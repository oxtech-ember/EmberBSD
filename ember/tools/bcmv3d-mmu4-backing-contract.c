/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Real backing/frame-helper/encoder TUs. Only kernel services are doubled. */
#ifdef BACKING_TYPES
#ifndef BACKING_SERVICES_H
#define BACKING_SERVICES_H
#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif
#define KM_SLEEP 0
#define BUS_DMA_WAITOK 0
#define BUS_DMA_WRITE 0x200
#define BUS_DMASYNC_PREWRITE 4
#define _BUS_DMAMAP_IS_BOUNCING 0x20000
#define container_of(p,t,m) ((t *)((char *)(p) - offsetof(t,m)))
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
typedef uint64_t bus_addr_t, bus_size_t, paddr_t, vmem_addr_t;
typedef uintptr_t vaddr_t;
typedef void *device_t, *ACPI_HANDLE;
typedef unsigned kmutex_t;
typedef void vmem_t;
struct drm_device { void *dev_private; };
struct drm_file;
struct uvm_pagerops;
struct drm_gem_object { struct drm_device *dev; size_t size; };
struct arm32_bus_dma_tag { unsigned token; };
typedef struct arm32_bus_dma_tag *bus_dma_tag_t;
typedef struct { uint64_t ds_addr, ds_len, _ds_paddr; } bus_dma_segment_t;
struct arm32_bus_dmamap {
	uint64_t dm_mapsize;
	int dm_nsegs, _dm_flags;
	void *_dm_iommu;
	bus_dma_segment_t dm_segs[1];
	unsigned region, kind;
};
typedef struct arm32_bus_dmamap *bus_dmamap_t;
struct drm_gem_cma_object {
	struct drm_gem_object base;
	bus_dma_tag_t dmat;
	bus_dmamap_t dmamap;
	void *vaddr;
	size_t dmasize;
};
#define to_drm_gem_cma_obj(p) container_of(p,struct drm_gem_cma_object,base)
void *kmem_zalloc(size_t, int);
void kmem_free(void *, size_t);
void backing_assert_sleepable(void);
#define ASSERT_SLEEPABLE() backing_assert_sleepable()
int cpu_intr_p(void);
int cpu_softintr_p(void);
void *pmap_kernel(void);
bool pmap_extract_coherency(void *, vaddr_t, paddr_t *, bool *);
int bus_dmamem_alloc(bus_dma_tag_t, bus_size_t, bus_size_t, bus_size_t,
    bus_dma_segment_t *, int, int *, int);
int bus_dmamem_map(bus_dma_tag_t, bus_dma_segment_t *, int, size_t, void **, int);
void bus_dmamem_unmap(bus_dma_tag_t, void *, size_t);
void bus_dmamem_free(bus_dma_tag_t, bus_dma_segment_t *, int);
int bus_dmamap_create(bus_dma_tag_t, bus_size_t, int, bus_size_t, bus_size_t,
    int, bus_dmamap_t *);
int bus_dmamap_load(bus_dma_tag_t, bus_dmamap_t, void *, bus_size_t, void *, int);
void bus_dmamap_unload(bus_dma_tag_t, bus_dmamap_t);
void bus_dmamap_destroy(bus_dma_tag_t, bus_dmamap_t);
void bus_dmamap_sync(bus_dma_tag_t, bus_dmamap_t, bus_addr_t, bus_size_t, int);
void drm_gem_object_put_unlocked(struct drm_gem_object *);
#endif
#else
#include "services.h"
#include <unistd.h>
#include <sys/wait.h>
#include "bcmv3d_drv.h"
#include "bcmv3d_hvs_prepare.h"
#include "bcmv3d_mmu4_backing.h"
#include <arm/broadcom/bcm2712_mmu4_corevar.h>
#include <arm/broadcom/bcm2712_mmu4_tablevar.h>

static unsigned long checks;
#define CHECK(c) do { ++checks; if (!(c)) { \
	fprintf(stderr,"CHECK %s:%u: %s\n", __FILE__, (unsigned)__LINE__, #c); abort(); \
} } while (0)

struct region {
	void *kva;
	uint64_t pa, size;
	bool mapped, allocated;
};
static struct region regions[5];
static struct arm32_bus_dma_tag tag;
static struct drm_device drm;
static struct bcmv3d_device device;
static struct bcmv3d_bo *bo;
static bus_dmamap_t consumer, region_maps[5];
static unsigned service_calls, fail_service, pmap_calls, fail_pmap;
static unsigned bad_pmap, bad_map, intr, allocations, maps, syncs;
static bool preparing, coherent_value, reenter;
static uint64_t frame_pa = 0x10000000, control_pa = 0x20000000;
static struct bcmv3d_mmu4_backing *current;
static unsigned frees[64], nfrees;
static uint32_t load32(const void *p)
{
	const uint8_t *b = p;
	return (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
	    ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}
static bool fail(void)
{ return ++service_calls == fail_service; }
static void freed(unsigned n)
{ CHECK(nfrees < 64); frees[nfrees++] = n; }
static void *aligned(size_t size)
{
	void *p = aligned_alloc(PAGE_SIZE, size);
	CHECK(p != NULL); memset(p, 0xa5, size); return p;
}
void *kmem_zalloc(size_t n, int flags)
{
	void *p;
	CHECK(flags == KM_SLEEP && !preparing);
	p = calloc(1, n); CHECK(p != NULL); allocations++; return p;
}
void kmem_free(void *p, size_t n)
{ CHECK(p != NULL && n != 0); allocations--; free(p); }
void backing_assert_sleepable(void) { CHECK(intr == 0); }
int cpu_intr_p(void) { return intr == 1; }
int cpu_softintr_p(void) { return intr == 2; }
void *pmap_kernel(void) { return &tag; }
bool pmap_extract_coherency(void *pm, vaddr_t va, paddr_t *pa, bool *coherent)
{
	uint64_t offset;
	unsigned i;

	CHECK(pm == &tag && pa != NULL && coherent != NULL);
	++pmap_calls;
	if (reenter) {
		struct bcm2712_mmu4_config cfg;
		reenter = false;
		CHECK(bcmv3d_mmu4_backing_get_config(current, &cfg) == 0);
		CHECK(bcmv3d_mmu4_backing_prepare(current, &cfg) == EBUSY);
		CHECK(bcmv3d_mmu4_backing_destroy_unexposed(current) == EBUSY);
		CHECK(bcmv3d_mmu4_backing_seal(current) == EBUSY);
	}
	if (pmap_calls == fail_pmap)
		return false;
	for (i = 0; i < 5; i++) {
		if (regions[i].kva == NULL || va < (uintptr_t)regions[i].kva)
			continue;
		offset = va - (uintptr_t)regions[i].kva;
		if (offset >= regions[i].size)
			continue;
		CHECK((offset & 4095) == 0 || (offset & 4095) == 4095);
		*pa = regions[i].pa + offset;
		if (pmap_calls == bad_pmap)
			*pa ^= 4096;
		*coherent = coherent_value;
		return true;
	}
	CHECK(false); return false;
}
int bus_dmamem_alloc(bus_dma_tag_t t, bus_size_t size, bus_size_t alignment,
    bus_size_t boundary, bus_dma_segment_t *segment, int max, int *n, int flags)
{
	unsigned i;

	CHECK(!preparing && t == &tag && size == PAGE_SIZE && alignment == PAGE_SIZE);
	CHECK(boundary == 0 && max == 1 && flags == BUS_DMA_WAITOK);
	if (fail()) return ENOMEM;
	for (i = 1; i < 5 && regions[i].allocated; i++)
		continue;
	CHECK(i < 5);
	regions[i] = (struct region){ .size = size,
	    .pa = control_pa + (i - 1) * UINT64_C(0x100000), .allocated = true };
	*segment = (bus_dma_segment_t){ .ds_addr = regions[i].pa, .ds_len = size,
	    ._ds_paddr = UINT64_MAX }; *n = 1; return 0;
}
int bus_dmamem_map(bus_dma_tag_t t, bus_dma_segment_t *segment, int n,
    size_t size, void **out, int flags)
{
	unsigned i;

	CHECK(!preparing && t == &tag && n == 1 && flags == BUS_DMA_WAITOK);
	if (fail()) return EIO;
	for (i = 1; i < 5 && regions[i].pa != segment->ds_addr; i++)
		continue;
	CHECK(i < 5 && regions[i].size == size && !regions[i].mapped);
	regions[i].kva = aligned(size); regions[i].mapped = true;
	*out = regions[i].kva; return 0;
}
void bus_dmamem_unmap(bus_dma_tag_t t, void *kva, size_t size)
{
	unsigned i;

	CHECK(t == &tag);
	for (i = 1; i < 5 && regions[i].kva != kva; i++)
		continue;
	CHECK(i < 5 && regions[i].mapped && regions[i].size == size);
	regions[i].mapped = false; freed(i * 10 + 3);
	free(kva); regions[i].kva = NULL;
}
void bus_dmamem_free(bus_dma_tag_t t, bus_dma_segment_t *segment, int n)
{
	unsigned i;

	CHECK(t == &tag && n == 1);
	for (i = 1; i < 5 && regions[i].pa != segment->ds_addr; i++)
		continue;
	CHECK(i < 5 && regions[i].allocated && !regions[i].mapped);
	regions[i].allocated = false; freed(i * 10 + 4);
}
int bus_dmamap_create(bus_dma_tag_t t, bus_size_t size, int n,
    bus_size_t max, bus_size_t boundary, int flags, bus_dmamap_t *out)
{
	bus_dmamap_t m;

	CHECK(!preparing && t == &tag && n == 1 && max == size && boundary == 0);
	CHECK(flags == BUS_DMA_WAITOK);
	if (fail()) return ENOMEM;
	m = calloc(1, sizeof(*m)); CHECK(m != NULL); maps++;
	*out = m; return 0;
}
int bus_dmamap_load(bus_dma_tag_t t, bus_dmamap_t m, void *kva,
    bus_size_t size, void *p, int flags)
{
	unsigned i;

	CHECK(!preparing && t == &tag && p == NULL);
	CHECK(flags == (BUS_DMA_WAITOK | BUS_DMA_WRITE));
	if (fail()) return EIO;
	for (i = 0; i < 5 && regions[i].kva != kva; i++)
		continue;
	CHECK(i < 5 && regions[i].size == size);
	region_maps[i] = m;
	m->region = i; m->kind = i == 0 ? 1 : 2;
	m->dm_nsegs = 1; m->dm_mapsize = size;
	m->dm_segs[0] = (bus_dma_segment_t){ .ds_addr = regions[i].pa,
	    .ds_len = size, ._ds_paddr = UINT64_MAX };
	if (i == 0) consumer = m;
	if (bad_map == i + 1) m->dm_segs[0].ds_addr += 4096;
	return 0;
}
void bus_dmamap_unload(bus_dma_tag_t t, bus_dmamap_t m)
{
	CHECK(t == &tag && m->dm_nsegs == 1);
	m->dm_nsegs = 0; freed(m->region * 10 + 1);
}
void bus_dmamap_destroy(bus_dma_tag_t t, bus_dmamap_t m)
{
	CHECK(t == &tag && m != NULL && m->dm_nsegs == 0);
	freed(m->region * 10 + 2); if (m == consumer) consumer = NULL;
	if (region_maps[m->region] == m) region_maps[m->region] = NULL;
	maps--; free(m);
}
static void image_check(void)
{
	uint64_t i, n;
	uint32_t expected;
	unsigned r;

	for (i = 0; i < regions[0].size; i++)
		CHECK(((uint8_t *)regions[0].kva)[i] == 0);
	for (r = 1; r < 5; r++) {
		for (i = 0; i < PAGE_SIZE / 4; i++) {
			expected = 0;
			if (r == 1 && i < 2)
				expected = 0x10000000 | (uint32_t)(regions[i + 2].pa >> 12);
			if ((r == 2 || r == 3) && i < 1024) {
				n = (r - 2) * UINT64_C(1024) + i;
				if (n < 2025)
					expected = 0x10000000 |
					    (uint32_t)((regions[0].pa + n * 4096) >> 12);
			}
			CHECK(load32((uint8_t *)regions[r].kva + i * 4) == expected);
		}
	}
}
void bus_dmamap_sync(bus_dma_tag_t t, bus_dmamap_t m, bus_addr_t offset,
    bus_size_t size, int flags)
{
	CHECK(preparing && t == &tag && offset == 0);
	CHECK(flags == BUS_DMASYNC_PREWRITE && size == regions[m->region].size);
	CHECK(m->region == (syncs < 4 ? syncs + 1 : 0));
	image_check(); syncs++;
}
struct bcmv3d_bo *bcmv3d_bo_alloc(struct bcmv3d_device *sc, uint32_t requested)
{
	size_t size = ((size_t)8294400 + PAGE_SIZE - 1) & ~((size_t)PAGE_SIZE - 1);

	CHECK(sc == &device && requested == 8294400 && !preparing && bo == NULL);
	if (fail()) return ERR_PTR(-ENOMEM);
	bo = calloc(1, sizeof(*bo)); CHECK(bo != NULL);
	regions[0] = (struct region){ .size = size, .pa = frame_pa,
	    .kva = aligned(size), .allocated = true, .mapped = true };
	bo->cma.base.dev = sc->drm; bo->cma.base.size = size;
	bo->cma.dmat = sc->dmat; bo->cma.vaddr = regions[0].kva;
	bo->cma.dmasize = size; bo->cma.dmamap = calloc(1, sizeof(*bo->cma.dmamap));
	CHECK(bo->cma.dmamap != NULL);
	bo->cma.dmamap->dm_nsegs = 1; bo->cma.dmamap->dm_mapsize = size;
	bo->cma.dmamap->dm_segs[0] = (bus_dma_segment_t){ .ds_addr = frame_pa,
	    .ds_len = size, ._ds_paddr = UINT64_MAX };
	bo->gpu = 0xfee00000; return bo;
}
void drm_gem_object_put_unlocked(struct drm_gem_object *obj)
{
	CHECK(bo != NULL && obj == &bo->cma.base && consumer == NULL);
	CHECK(!regions[1].allocated && !regions[2].allocated &&
	    !regions[3].allocated && !regions[4].allocated);
	free(bo->cma.dmamap); free(regions[0].kva); free(bo); bo = NULL;
	memset(&regions[0], 0, sizeof(regions[0])); freed(9);
}
static void reset(void)
{
	unsigned i;

	CHECK(allocations == 0 && maps == 0 && bo == NULL);
	for (i = 0; i < 5; i++) CHECK(!regions[i].allocated);
	memset(regions, 0, sizeof(regions));
	device = (struct bcmv3d_device){ .drm = &drm, .dmat = &tag };
	drm.dev_private = &device;
	service_calls = fail_service = pmap_calls = fail_pmap = bad_pmap = 0;
	bad_map = intr = syncs = nfrees = 0; reenter = false; preparing = false;
	coherent_value = false; frame_pa = 0x10000000; control_pa = 0x20000000;
}
static struct bcmv3d_mmu4_backing *create(void)
{
	struct bcmv3d_mmu4_backing *b = NULL;

	CHECK(bcmv3d_mmu4_backing_create(&device, 0x20804774, &b) == 0);
	CHECK(b != NULL && bo != NULL && consumer != NULL); return b;
}
static void prepare(struct bcmv3d_mmu4_backing *b, int expected)
{
	struct bcm2712_mmu4_config cfg;

	CHECK(bcmv3d_mmu4_backing_get_config(b, &cfg) == 0);
	preparing = true;
	CHECK(bcmv3d_mmu4_backing_prepare(b, &cfg) == expected);
	preparing = false;
}
static void positive(void)
{
	struct bcmv3d_mmu4_backing_status st;
	struct bcm2712_mmu4_config cfg;
	struct bcmv3d_mmu4_backing *b;
	unsigned i, expected_fragments = (PAGE_SIZE == 4096 ? 2029 : 2096);

	reset(); b = create(); current = b;
	CHECK(service_calls == 19 && pmap_calls == expected_fragments * 2);
	CHECK(bcmv3d_mmu4_backing_get_config(b, &cfg) == 0);
	CHECK(cfg.top_pa == 0x20000000 && cfg.l2_pa[0] == 0x20100000 &&
	    cfg.l2_pa[1] == 0x20200000 && cfg.default_pa == 0x20300000);
	reenter = true; prepare(b, 0); CHECK(syncs == 5); image_check();
	CHECK(bcmv3d_mmu4_backing_status(b, &st) == 0);
	CHECK(st.prepared && !st.sealed && st.first_error == 0 && st.syncs == 5);
	CHECK(st.fragments == expected_fragments && st.pmap_calls == expected_fragments * 2);
	CHECK(st.stage == BCMV3D_MMU4_BACKING_PREPARED);
	CHECK(bcmv3d_mmu4_backing_prepare(b, &cfg) == EBUSY);
	CHECK(bcmv3d_mmu4_backing_destroy_unexposed(b) == 0);
	CHECK(nfrees == 19);
	for (i = 0; i < 16; i++) CHECK(frees[i] == (4 - i / 4) * 10 + i % 4 + 1);
	CHECK(frees[16] == 1 && frees[17] == 2 && frees[18] == 9);
}
static void
isolated(unsigned scenario)
{
	pid_t child;
	int status;

	child = fork(); CHECK(child != -1);
	if (child == 0) {
		struct bcmv3d_mmu4_backing_status state;
		struct bcmv3d_mmu4_backing *b;
		unsigned before;
		int expected = EOPNOTSUPP;

		reset(); b = create(); current = b;
		switch (scenario) {
		case 0: fail_pmap = pmap_calls + 1; expected = EFAULT; break;
		case 1: bad_pmap = pmap_calls + 34; expected = EAGAIN; break;
		case 2: bo->cma.dmamap->_dm_flags = _BUS_DMAMAP_IS_BOUNCING; break;
		case 3: consumer->_dm_iommu = &tag; break;
		case 4: bo->cma.dmamap->dm_segs[0].ds_addr++; break;
		case 5: bo->cma.dmamap->_dm_iommu = &tag; break;
		case 6: consumer->_dm_flags = _BUS_DMAMAP_IS_BOUNCING; break;
		case 7: bo->cma.dmamap->dm_mapsize--; expected = EINVAL; break;
		case 8: bo->cma.dmamap->dm_segs[0].ds_len--; expected = EINVAL; break;
		case 9: bo->cma.dmamap->dm_nsegs = 2; expected = EINVAL; break;
		case 10: bo->busy = 1; expected = EINVAL; break;
		case 11: region_maps[4]->_dm_iommu = &tag; break;
		case 12: region_maps[3]->_dm_flags = _BUS_DMAMAP_IS_BOUNCING; break;
		case 13: region_maps[2]->dm_segs[0].ds_addr++; break;
		case 14: bad_pmap = pmap_calls * 2; expected = EAGAIN; break;
		default: CHECK(false);
		}
		prepare(b, expected);
		CHECK(bcmv3d_mmu4_backing_status(b, &state) == 0);
		CHECK(state.mapping_uncertain && state.first_error == expected);
		CHECK(!state.sealed && !state.prepared && syncs == 0);
		before = nfrees;
		CHECK(bcmv3d_mmu4_backing_destroy_unexposed(b) == EBUSY);
		CHECK(nfrees == before && bo != NULL && maps == 5);
		/* The process retains the uncertain object. No metadata restoration. */
		_exit(0);
	}
	CHECK(waitpid(child, &status, 0) == child);
	CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

int
main(void)
{
	struct bcmv3d_mmu4_backing *b, *poison = (void *)(uintptr_t)0x1234;
	struct bcmv3d_mmu4_backing_status st, saved;
	struct bcm2712_mmu4_config cfg;
	unsigned i, total;

	if (PAGE_SIZE != 4096 && PAGE_SIZE != 65536) {
		reset(); b = poison;
		CHECK(bcmv3d_mmu4_backing_create(&device, 0x20804774, &b) == EOPNOTSUPP);
		CHECK(b == poison && service_calls == 0);
		puts("PASS: unsupported native page size refused before effects");
		return 0;
	}
	positive(); reset();
	memset(&st, 0xa5, sizeof(st)); memcpy(&saved, &st, sizeof(saved));
	CHECK(bcmv3d_mmu4_backing_status(NULL, &st) == EINVAL);
	CHECK(memcmp(&st, &saved, sizeof(st)) == 0);
	CHECK(bcmv3d_mmu4_backing_create(NULL, 0, &poison) == EINVAL);
	CHECK(poison == (void *)(uintptr_t)0x1234 && service_calls == 0);
	for (i = 1; i <= 19; i++) {
		reset(); fail_service = i; b = poison;
		CHECK(bcmv3d_mmu4_backing_create(&device, 0x20804774, &b) != 0);
		CHECK(b == poison && service_calls == i && syncs == 0);
	}
	reset(); b = create(); total = pmap_calls;
	CHECK(bcmv3d_mmu4_backing_destroy_unexposed(b) == 0);
	for (i = 1; i <= (getenv("BACKING_SHORT_FAILURES") ? 2 : total); i++) {
		reset(); fail_pmap = i; b = poison;
		CHECK(bcmv3d_mmu4_backing_create(&device, 0x20804774, &b) == EFAULT);
		CHECK(b == poison && pmap_calls == i && syncs == 0);
	}
	for (i = 1; i <= 5; i++) {
		reset(); bad_map = i; b = poison;
		CHECK(bcmv3d_mmu4_backing_create(&device, 0x20804774, &b) == EOPNOTSUPP);
		CHECK(b == poison && syncs == 0);
	}
	for (i = 1; i <= 2; i++) {
		reset(); intr = i; b = poison;
		CHECK(bcmv3d_mmu4_backing_create(&device, 0x20804774, &b) == EWOULDBLOCK);
		CHECK(b == poison && service_calls == 0);
	}
	reset(); b = create(); CHECK(bcmv3d_mmu4_backing_get_config(b, &cfg) == 0);
	cfg.default_pa++; preparing = true;
	CHECK(bcmv3d_mmu4_backing_prepare(b, &cfg) == EINVAL); preparing = false;
	CHECK(syncs == 0 && bcmv3d_mmu4_backing_seal(b) == EIO);
	CHECK(bcmv3d_mmu4_backing_destroy_unexposed(b) == 0);
	for (i = 0; i < 5; i++) isolated(i);
	reset(); frame_pa = 0; b = create(); prepare(b, 0);
	CHECK(bcmv3d_mmu4_backing_destroy_unexposed(b) == 0);
	reset(); frame_pa = (UINT64_C(1) << 32) -
	    (((UINT64_C(8294400) + PAGE_SIZE - 1) & ~((uint64_t)PAGE_SIZE - 1)));
	b = create(); prepare(b, 0); CHECK(bcmv3d_mmu4_backing_destroy_unexposed(b) == 0);
	reset(); frame_pa = UINT64_C(1) << 32; b = poison;
	CHECK(bcmv3d_mmu4_backing_create(&device, 0x20804774, &b) != 0 && b == poison);
	reset(); control_pa = 0; b = poison;
	CHECK(bcmv3d_mmu4_backing_create(&device, 0x20804774, &b) == EINVAL && b == poison);
	reset(); control_pa = frame_pa; b = poison;
	CHECK(bcmv3d_mmu4_backing_create(&device, 0x20804774, &b) == EINVAL && b == poison);
	reset(); b = create(); coherent_value = true; prepare(b, 0);
	CHECK(syncs == 5 && bcmv3d_mmu4_backing_destroy_unexposed(b) == 0);
	for (i = 5; i < 14; i++) isolated(i);
	for (i = 0; i < 5; i++) {
		reset(); b = create();
		CHECK(bcmv3d_mmu4_backing_get_config(b, &cfg) == 0);
		switch (i) {
		case 0: cfg.debug++; break;
		case 1: cfg.top_pa++; break;
		case 2: cfg.l2_pa[0]++; break;
		case 3: cfg.l2_pa[1]++; break;
		case 4: cfg.default_pa++; break;
		}
		preparing = true;
		CHECK(bcmv3d_mmu4_backing_prepare(b, &cfg) == EINVAL);
		CHECK(bcmv3d_mmu4_backing_prepare(b, &cfg) == EBUSY);
		preparing = false;
		CHECK(syncs == 0 && bcmv3d_mmu4_backing_destroy_unexposed(b) == 0);
	}
	reset(); b = poison;
	CHECK(bcmv3d_mmu4_backing_create(&device, 0, &b) == EOPNOTSUPP && b == poison);
	reset(); control_pa = (UINT64_C(1) << 32) - UINT64_C(0x300000) - PAGE_SIZE;
	b = create(); prepare(b, 0);
	CHECK(bcmv3d_mmu4_backing_destroy_unexposed(b) == 0);
	reset(); control_pa = UINT64_C(1) << 32; b = poison;
	CHECK(bcmv3d_mmu4_backing_create(&device, 0x20804774, &b) == ERANGE && b == poison);
	isolated(14);
	for (i = 1; i <= 2; i++) {
		reset(); b = create();
		CHECK(bcmv3d_mmu4_backing_get_config(b, &cfg) == 0);
		intr = i;
		CHECK(bcmv3d_mmu4_backing_prepare(b, &cfg) == EWOULDBLOCK);
		CHECK(bcmv3d_mmu4_backing_destroy_unexposed(b) == EWOULDBLOCK);
		intr = 0; prepare(b, 0);
		CHECK(bcmv3d_mmu4_backing_destroy_unexposed(b) == 0);
	}
	reset(); b = create(); current = b;
	CHECK(bcmv3d_mmu4_backing_seal(b) == 0);
	CHECK(bcmv3d_mmu4_backing_seal(b) == EBUSY);
	if (getenv("BACKING_SEALED_FAILURE")) fail_pmap = pmap_calls + 1;
	prepare(b, fail_pmap ? EFAULT : 0);
	CHECK(bcmv3d_mmu4_backing_destroy_unexposed(b) == EBUSY);
	CHECK(bcmv3d_mmu4_backing_status(b, &st) == 0 && st.sealed);
	CHECK(st.prepared == (fail_pmap == 0));
	/* Sealed storage is deliberately retained until this process exits. */
	printf("PASS: %lu native backing checks (PAGE_SIZE %u)\n", checks, (unsigned)PAGE_SIZE);
	return 0;
}
#endif
