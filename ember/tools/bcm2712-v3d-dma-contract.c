/* Origin: EmberBSD actual-source V3D DMA probe contract, 2026-10-10. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

/*
 * Host contract for the bounded BCM2712 V3D first DMA/TFU experiment.
 * It compiles the production probe body and substitutes only host
 * services: the sealed takeover window, bus_dma and a fake GPU whose
 * MMU honors the published page table. The fake I/O models a separate
 * CPU and device view of noncoherent memory: CPU writes reach the GPU
 * only after PREWRITE, GPU writes reach the CPU only after POSTREAD.
 * This cannot establish physical V3D behavior; it checks the probe's
 * decisions, ordering, fault priority and retention rules.
 */

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef __BIT
#define __BIT(n) (UINT32_C(1) << (n))
#endif
#ifndef __BITS
#define __BITS(hi, lo) (((UINT64_C(1) << ((hi) + 1)) - 1) & \
    ~((UINT64_C(1) << (lo)) - 1))
#endif
#ifndef __SHIFTOUT
#define __SHIFTOUT(x, mask) (((x) & (mask)) / ((mask) & (~(mask) + 1)))
#endif
#ifndef __arraycount
#define __arraycount(a) (sizeof((a)) / sizeof((a)[0]))
#endif

#define PAGE_SIZE 4096
#define KASSERT(v) assert(v)

#define BUS_DMA_WAITOK 0x0
#define BUS_DMASYNC_PREREAD 0x01
#define BUS_DMASYNC_POSTREAD 0x02
#define BUS_DMASYNC_PREWRITE 0x04
#define BUS_DMASYNC_POSTWRITE 0x08

typedef uint64_t bus_addr_t;
typedef size_t bus_size_t;
typedef struct fake_dma_tag *bus_dma_tag_t;
typedef struct device { unsigned int id; } *device_t;

struct fake_segment {
	bus_addr_t ds_addr;
	bus_size_t ds_len;
};
typedef struct fake_segment bus_dma_segment_t;

struct fake_map {
	int dm_nsegs;
	bus_dma_segment_t dm_segs[2];
	int object;
	bool destroyed;
};
typedef struct fake_map *bus_dmamap_t;

struct fake_allocation {
	uint8_t *cpu;		/* what the CPU reads and writes */
	uint8_t *dev;		/* what the GPU reads and writes */
	bus_size_t size;
	bus_addr_t base;	/* the DMA address seen by hardware */
	bus_dmamap_t map;
	bool live;
};

struct fake_dma_tag {
	int unused;
};

/* --- production body ---------------------------------------------- */

static void fake_aprint(device_t, const char *, ...);
static void delay(unsigned int);
static bool fake_takeover_complete;
static unsigned int mmio_pokes, fail_peek_at, fail_poke_at;
static unsigned int allocs, maps, creates, loads;
static unsigned int frees, unmaps, destroys, unloads;
static int current_object;
static bool inside_driver;

static int fake_bus_dmamem_alloc(bus_dma_tag_t, bus_size_t, bus_size_t,
    bus_size_t, bus_dma_segment_t *, int, int *, int);
static int fake_bus_dmamem_map(bus_dma_tag_t, bus_dma_segment_t *, int,
    bus_size_t, void **, int);
static void fake_bus_dmamem_unmap(bus_dma_tag_t, void *, bus_size_t);
static void fake_bus_dmamem_free(bus_dma_tag_t, bus_dma_segment_t *, int);
static int fake_bus_dmamap_create(bus_dma_tag_t, bus_size_t, int,
    bus_size_t, int, int, bus_dmamap_t *);
static void fake_bus_dmamap_destroy(bus_dma_tag_t, bus_dmamap_t);
static int fake_bus_dmamap_load(bus_dma_tag_t, bus_dmamap_t, void *,
    bus_size_t, void *, int);
static void fake_bus_dmamap_unload(bus_dma_tag_t, bus_dmamap_t);
static void fake_bus_dmamap_sync(bus_dma_tag_t, bus_dmamap_t, bus_addr_t,
    bus_size_t, int);

#define bus_dmamem_alloc fake_bus_dmamem_alloc
#define bus_dmamem_map fake_bus_dmamem_map
#define bus_dmamem_unmap fake_bus_dmamem_unmap
#define bus_dmamem_free fake_bus_dmamem_free
#define bus_dmamap_create fake_bus_dmamap_create
#define bus_dmamap_destroy fake_bus_dmamap_destroy
#define bus_dmamap_load fake_bus_dmamap_load
#define bus_dmamap_unload fake_bus_dmamap_unload
#define bus_dmamap_sync fake_bus_dmamap_sync
#define aprint_normal_dev fake_aprint
#define bcmv3d_takeover_hub_peek fake_hub_peek
#define bcmv3d_takeover_hub_poke fake_hub_poke
static void fake_aprint(device_t, const char *, ...);
static int fake_hub_peek(bus_size_t, uint32_t *);
static int fake_hub_poke(bus_size_t, uint32_t);
bool bcmv3d_takeover_complete(void);

#include "dma-body.h"

/* --- register file ------------------------------------------------- */

enum {
	R_MASK_STS = 0x5c, R_MMUC_CTL = 0x1000, R_MMU_CTL = 0x1200,
	R_PT_BASE = 0x1204, R_ILLEGAL = 0x1230, R_MMU_DEBUG = 0x1238,
	R_TFU_CS = 0x700, R_TFU_ICFG = 0x708, R_TFU_IIA = 0x70c,
	R_TFU_ICA = 0x710, R_TFU_IIS = 0x714, R_TFU_IUA = 0x718,
	R_TFU_IOC = 0x71c, R_TFU_IOA = 0x720, R_TFU_IOS = 0x724,
	R_TFU_COEF0 = 0x728, R_TFU_COEF3 = 0x734
};

static struct {
	uint32_t mmuc_ctl, mmu_ctl, pt_base, illegal;
	uint32_t tfu_cs, tfu_icfg, tfu_iia, tfu_ica, tfu_iis, tfu_iua;
	uint32_t tfu_ioc, tfu_ioa, tfu_ios, tfu_coef[4];
	uint32_t mask_sts, mmu_debug;
} reg;

static unsigned int poke_order[64], poke_count;
static unsigned int pre_syncs_before_publication;
static bool publication_seen;

enum gpu_mode {
	GPU_TRANSLATED,		/* honor the published page table */
	GPU_IDENTITY,		/* ignore PTEs, use VA as PA */
	GPU_FAULT,		/* fault and still bump the counter */
	GPU_TIMEOUT,		/* never complete */
	GPU_DOUBLE,		/* bump the counter twice */
	GPU_GARBAGE,		/* translate, but copy garbage */
	GPU_TOUCH_SOURCE,	/* translated copy plus a source write */
	GPU_TOUCH_SCRATCH,	/* translated copy plus a scratch write */
	GPU_MIXED		/* translated copy plus an identity copy */
};

static struct fixture {
	enum gpu_mode mode;
	bool flush_stuck, tlb_stuck;
	bool fail_alloc[DV3D_OBJ_COUNT];
	int fail_map, fail_create, fail_load;	/* -1 never, else object id */
	int short_load, split_load, misalign_obj, above4g_obj, overlap_obj;
	uint8_t cvt_start;
	unsigned int delays;
	char last_message[512];
	const char *case_name;
} fx;

static struct device dev = { 1 };
static struct fake_dma_tag tag;

static void
fake_aprint(device_t dv, const char *fmt, ...)
{
	va_list ap;

	(void)dv;
	va_start(ap, fmt);
	vsnprintf(fx.last_message, sizeof(fx.last_message), fmt, ap);
	va_end(ap);
}

static void
delay(unsigned int us)
{
	(void)us;
	fx.delays++;
}

bool
bcmv3d_takeover_complete(void);
bool
bcmv3d_takeover_complete(void)
{

	return fake_takeover_complete;
}

/* --- fake bus_dma -------------------------------------------------- */

static struct fake_allocation allocations[DV3D_OBJ_COUNT];
static bus_addr_t default_bases[DV3D_OBJ_COUNT] = {
	0x10000000, 0x10400000, 0x10401000, 0x10406000, 0x1040b000, 0x10410000
};

static struct fake_allocation *
allocation_by_seg(const bus_dma_segment_t *seg)
{
	int i;

	for (i = 0; i < DV3D_OBJ_COUNT; i++)
		if (seg == (bus_dma_segment_t *)&dv3d.obj[i].seg)
			return &allocations[i];
	return NULL;
}

static struct fake_allocation *
allocation_by_base(bus_addr_t base)
{
	int i;

	for (i = 0; i < DV3D_OBJ_COUNT; i++)
		if (allocations[i].live && allocations[i].base == base)
			return &allocations[i];
	return NULL;
}

static struct fake_allocation *
allocation_containing(bus_addr_t addr)
{
	int i;

	for (i = 0; i < DV3D_OBJ_COUNT; i++)
		if (allocations[i].live && addr >= allocations[i].base &&
		    addr < allocations[i].base + allocations[i].size)
			return &allocations[i];
	return NULL;
}

static int
fake_bus_dmamem_alloc(bus_dma_tag_t t, bus_size_t size, bus_size_t align,
    bus_size_t boundary, bus_dma_segment_t *segs, int nsegs, int *rsegs,
    int flags)
{
	struct fake_allocation *allocation = allocation_by_seg(segs);

	(void)t;
	(void)align;
	(void)boundary;
	(void)nsegs;
	(void)flags;
	assert(inside_driver);
	assert(allocation != NULL);
	current_object = (int)(allocation - allocations);
	if (fx.fail_alloc[current_object]) {
		*rsegs = 0;
		return ENOMEM;
	}
	memset(allocation, 0, sizeof(*allocation));
	allocation->cpu = calloc(1, size);
	allocation->dev = calloc(1, size);
	allocation->size = size;
	allocation->base = default_bases[current_object];
	if (fx.overlap_obj == current_object)
		allocation->base = default_bases[DV3D_OBJ_ACTUAL_DST] + PAGE_SIZE;
	if (fx.above4g_obj == current_object)
		allocation->base = 0xffffe000;
	if (fx.misalign_obj == current_object)
		allocation->base += 0x123;
	allocation->live = true;
	segs->ds_addr = allocation->base;
	segs->ds_len = size;
	*rsegs = 1;
	allocs++;
	return 0;
}

static int
fake_bus_dmamem_map(bus_dma_tag_t t, bus_dma_segment_t *segs, int nsegs,
    bus_size_t size, void **kvap, int flags)
{
	struct fake_allocation *allocation = allocation_by_seg(segs);

	(void)t;
	(void)nsegs;
	(void)size;
	(void)flags;
	assert(allocation != NULL && allocation->live);
	if (fx.fail_map == (int)(allocation - allocations))
		return ENOMEM;
	*kvap = allocation->cpu;
	maps++;
	return 0;
}

static void
fake_bus_dmamem_unmap(bus_dma_tag_t t, void *kva, bus_size_t size)
{
	int i;

	(void)t;
	(void)size;
	/* GPU-exposed allocations are never unmapped. */
	assert(!dv3d.published);
	for (i = 0; i < DV3D_OBJ_COUNT; i++)
		if (allocations[i].live && allocations[i].cpu == kva)
			allocations[i].map = NULL;
	unmaps++;
}

static void
fake_bus_dmamem_free(bus_dma_tag_t t, bus_dma_segment_t *segs, int nsegs)
{
	struct fake_allocation *allocation = allocation_by_seg(segs);

	(void)t;
	(void)nsegs;
	/* GPU-exposed allocations are never released. */
	assert(!dv3d.published);
	assert(allocation != NULL);
	free(allocation->cpu);
	free(allocation->dev);
	memset(allocation, 0, sizeof(*allocation));
	frees++;
}

static int
fake_bus_dmamap_create(bus_dma_tag_t t, bus_size_t size, int nsegs,
    bus_size_t maxsegsz, int boundary, int flags, bus_dmamap_t *map)
{
	struct fake_map *m;

	(void)t;
	(void)size;
	(void)nsegs;
	(void)maxsegsz;
	(void)boundary;
	(void)flags;
	assert(inside_driver);
	if (fx.fail_create == current_object)
		return ENOMEM;
	m = calloc(1, sizeof(*m));
	m->object = current_object;
	*map = m;
	allocations[current_object].map = m;
	creates++;
	return 0;
}

static void
fake_bus_dmamap_destroy(bus_dma_tag_t t, bus_dmamap_t map)
{
	(void)t;
	assert(!dv3d.published);
	assert(map != NULL && !map->destroyed);
	map->destroyed = true;
	if (allocations[map->object].map == map)
		allocations[map->object].map = NULL;
	free(map);
	destroys++;
}

static int
fake_bus_dmamap_load(bus_dma_tag_t t, bus_dmamap_t map, void *kva,
    bus_size_t size, void *proc, int flags)
{
	struct fake_allocation *allocation = NULL;
	int i;

	(void)t;
	(void)proc;
	(void)flags;
	for (i = 0; i < DV3D_OBJ_COUNT; i++)
		if (allocations[i].live && allocations[i].cpu == kva)
			allocation = &allocations[i];
	assert(allocation != NULL);
	if (fx.fail_load == (int)(allocation - allocations))
		return ENOMEM;
	loads++;
	map->object = (int)(allocation - allocations);
	map->dm_nsegs = fx.split_load == map->object ? 2 : 1;
	map->dm_segs[0].ds_addr = allocation->base;
	map->dm_segs[0].ds_len =
	    fx.short_load == map->object ? size - PAGE_SIZE : size;
	if (map->dm_nsegs == 2) {
		map->dm_segs[1].ds_addr = allocation->base + size / 2;
		map->dm_segs[1].ds_len = size / 2;
	}
	return 0;
}

static void
fake_bus_dmamap_unload(bus_dma_tag_t t, bus_dmamap_t map)
{
	(void)t;
	assert(!dv3d.published);
	unloads++;
}

static void
fake_bus_dmamap_sync(bus_dma_tag_t t, bus_dmamap_t map, bus_addr_t offset,
    bus_size_t length, int ops)
{
	struct fake_allocation *allocation = &allocations[map->object];

	(void)t;
	if (!publication_seen)
		pre_syncs_before_publication++;
	/*
	 * Noncoherent model: PREWRITE publishes CPU writes to the GPU and
	 * POSTREAD publishes GPU writes to the CPU. The probe relies on
	 * both directions; dropping either side must break the contract.
	 */
	if ((ops & BUS_DMASYNC_PREWRITE) != 0)
		memcpy(allocation->dev + offset, allocation->cpu + offset, length);
	if ((ops & BUS_DMASYNC_POSTREAD) != 0)
		memcpy(allocation->cpu + offset, allocation->dev + offset, length);
}

/* --- fake GPU ------------------------------------------------------ */

static bool
fake_translate(bus_addr_t va, bus_addr_t *pa)
{
	struct fake_allocation *pt = allocation_by_base(
	    (bus_addr_t)reg.pt_base << DV3D_PAGE_SHIFT);
	uint32_t pte;

	assert(pt != NULL && pt->dev != NULL);
	if ((va >> DV3D_PAGE_SHIFT) >= pt->size / 4)
		return false;
	memcpy(&pte, pt->dev + (va >> DV3D_PAGE_SHIFT) * 4, sizeof(pte));
	if ((pte & DV3D_PTE_VALID) == 0)
		return false;
	*pa = ((bus_addr_t)(pte & 0x00ffffff) << DV3D_PAGE_SHIFT) |
	    (va & (PAGE_SIZE - 1));
	return true;
}

static void
fake_gpu_copy(bus_addr_t src, bus_addr_t dst, bool identity, bool garbage)
{
	struct fake_allocation *src_mem, *dst_mem;
	bus_addr_t src_pa, dst_pa;
	bus_size_t done, i, chunk;

	/*
	 * The hardware reads 16 KiB plus the 64-byte read-ahead allowance
	 * from the source, but writes only the image itself; the extra
	 * destination page is an untouched canary. Every touched page
	 * walks the MMU like the hardware would.
	 */
	for (done = DV3D_TFU_DATA; done < DV3D_TFU_DATA + 64; done += PAGE_SIZE)
		if (!identity && !fake_translate(src + done, &src_pa)) {
			reg.mmu_ctl |= __BIT(20);
			return;
		}
	for (done = 0; done < DV3D_TFU_DATA; done += chunk) {
		chunk = PAGE_SIZE - ((src + done) & (PAGE_SIZE - 1));
		if (chunk > DV3D_TFU_DATA - done)
			chunk = DV3D_TFU_DATA - done;
		if (chunk > PAGE_SIZE - ((dst + done) & (PAGE_SIZE - 1)))
			chunk = PAGE_SIZE - ((dst + done) & (PAGE_SIZE - 1));
		if (identity) {
			src_pa = src + done;
			dst_pa = dst + done;
		} else if (!fake_translate(src + done, &src_pa) ||
		    !fake_translate(dst + done, &dst_pa)) {
			reg.mmu_ctl |= __BIT(20);
			return;
		}
		src_mem = allocation_containing(src_pa);
		dst_mem = allocation_containing(dst_pa);
		assert(src_mem != NULL && dst_mem != NULL);
		for (i = 0; i < chunk; i++) {
			size_t s = (src_pa - src_mem->base) + i;
			size_t d = (dst_pa - dst_mem->base) + i;

			dst_mem->dev[d] = garbage ? (uint8_t)((done + i) * 31 + 7) :
			    src_mem->dev[s];
		}
	}
}

static void
fake_gpu_execute(enum gpu_mode mode)
{
	uint32_t count = (reg.tfu_cs >> 16) & 0xff;

	if (mode == GPU_TIMEOUT)
		return;
	if (mode == GPU_FAULT) {
		/* The fault and the counter move at the same time. */
		reg.mmu_ctl |= __BIT(20);
		reg.tfu_cs = (reg.tfu_cs & ~UINT32_C(0x00ff0000)) |
		    (((count + 1) & 0xff) << 16);
		return;
	}
	if (mode == GPU_IDENTITY) {
		fake_gpu_copy(reg.tfu_iia, reg.tfu_ioa, true, false);
		count++;
		reg.tfu_cs = (reg.tfu_cs & ~UINT32_C(0x00ff0000)) |
		    ((count & 0xff) << 16);
		return;
	}
	if (mode == GPU_MIXED)
		fake_gpu_copy(reg.tfu_iia, reg.tfu_ioa, true, false);
	fake_gpu_copy(reg.tfu_iia, reg.tfu_ioa, false,
	    mode == GPU_GARBAGE);
	if (mode == GPU_TOUCH_SOURCE) {
		/* The GPU rewrites one word of the translated source. */
		bus_addr_t pa;

		if (fake_translate(reg.tfu_iia + 0x40, &pa)) {
			struct fake_allocation *target = allocation_containing(pa);
			uint32_t touch = 0x11223344;

			assert(target != NULL);
			memcpy(target->dev + (pa - target->base), &touch,
			    sizeof(touch));
		}
	}
	if (mode == GPU_TOUCH_SCRATCH) {
		struct fake_allocation *scratch = allocation_by_base(
		    (bus_addr_t)(reg.illegal & ~DV3D_MMU_ILLEGAL_ENABLE) <<
		    DV3D_PAGE_SHIFT);

		assert(scratch != NULL && scratch->dev != NULL);
		scratch->dev[0] = 1;
	}
	if (mode == GPU_DOUBLE)
		count++;
	count++;
	reg.tfu_cs = (reg.tfu_cs & ~UINT32_C(0x00ff0000)) |
	    ((count & 0xff) << 16);
}

/* --- sealed HUB window --------------------------------------------- */

static unsigned int peek_counter;

static int
fake_hub_peek(bus_size_t offset, uint32_t *value)
{

	if (fail_peek_at != 0 && ++peek_counter == fail_peek_at)
		return EIO;
	switch (offset) {
	case R_MASK_STS:
		*value = reg.mask_sts;
		break;
	case R_MMU_DEBUG:
		*value = reg.mmu_debug;
		break;
	case R_MMUC_CTL:
		*value = reg.mmuc_ctl;
		break;
	case R_MMU_CTL:
		*value = reg.mmu_ctl;
		break;
	case R_PT_BASE:
		*value = reg.pt_base;
		break;
	case R_ILLEGAL:
		*value = reg.illegal;
		break;
	case R_TFU_CS:
		*value = reg.tfu_cs;
		break;
	default:
		assert(offset >= R_TFU_ICFG && offset <= R_TFU_COEF3);
		*value = 0;
		break;
	}
	return 0;
}

static int
fake_hub_poke(bus_size_t offset, uint32_t value)
{
	int i;

	assert(poke_count < __arraycount(poke_order));
	poke_order[poke_count++] = (unsigned int)offset;
	if (fail_poke_at != 0 && ++mmio_pokes == fail_poke_at)
		return EIO;
	if (offset == R_PT_BASE)
		publication_seen = true;
	switch (offset) {
	case R_MMUC_CTL:
		/* Only the documented MMUC writes ENABLE=1, FLUSH|ENABLE=3. */
		assert(value == 1 || value == 3);
		reg.mmuc_ctl = (value & 3) |
		    (value == 3 && fx.flush_stuck ? __BIT(2) : 0);
		break;
	case R_MMU_CTL:
		/* Fault bits are never part of a deliberate write. */
		assert((value & ~(DV3D_MMU_CTL_TLB_CLEAR |
		    DV3D_MMU_CTL_TLB_CLEARING)) == DV3D_MMU_CTL_VALUE);
		assert((value & DV3D_MMU_CTL_FAULTS) == 0);
		reg.mmu_ctl = value & ~DV3D_MMU_CTL_TLB_CLEAR;
		if ((value & DV3D_MMU_CTL_TLB_CLEAR) != 0 && fx.tlb_stuck)
			reg.mmu_ctl |= DV3D_MMU_CTL_TLB_CLEARING;
		break;
	case R_PT_BASE:
		reg.pt_base = value;
		break;
	case R_ILLEGAL:
		reg.illegal = value;
		break;
	case R_TFU_ICFG:
		assert(value == DV3D_TFU_ICFG_VALUE);
		reg.tfu_icfg = value;
		fake_gpu_execute(fx.mode);
		break;
	case R_TFU_IIA:
		reg.tfu_iia = value;
		break;
	case R_TFU_IIS:
		/* Raster R32F: source stride 256 bytes = 64 texels. */
		assert(value == DV3D_TFU_DIM);
		reg.tfu_iis = value;
		break;
	case R_TFU_ICA:
		reg.tfu_ica = value;
		break;
	case R_TFU_IUA:
		reg.tfu_iua = value;
		break;
	case R_TFU_IOC:
		/* Output stride 64 texels << 16, raster output, no mips. */
		assert(value == DV3D_TFU_IOC_VALUE);
		reg.tfu_ioc = value;
		break;
	case R_TFU_IOA:
		reg.tfu_ioa = value;
		break;
	case R_TFU_IOS:
		/* Image size: 64 << 16 | 64. */
		assert(value == DV3D_TFU_IOS_VALUE);
		reg.tfu_ios = value;
		break;
	default:
		assert(offset >= R_TFU_COEF0 && offset <= R_TFU_COEF3);
		i = (int)(offset - R_TFU_COEF0) / 4;
		reg.tfu_coef[i] = value;
		break;
	}
	return 0;
}

/* --- cases ---------------------------------------------------------- */

static unsigned int cases, checks;
static const char *case_name;

#define CHECK(cond) do {						\
	checks++;							\
	if (!(cond)) {							\
		printf("FAIL case '%s' line %d: %s (last: %s)\n",	\
		    case_name, __LINE__, #cond, fx.last_message);	\
		exit(1);						\
	}								\
} while (0)

static void
reset_fixture(void)
{
	int i;

	if (dv3d.published)
		for (i = 0; i < DV3D_OBJ_COUNT; i++)
			if (allocations[i].live) {
				free(allocations[i].cpu);
				free(allocations[i].dev);
				if (allocations[i].map != NULL &&
				    !allocations[i].map->destroyed)
					free(allocations[i].map);
			}
	memset(&dv3d, 0, sizeof(dv3d));
	memset(allocations, 0, sizeof(allocations));
	memset(&reg, 0, sizeof(reg));
	memset(&fx, 0, sizeof(fx));
	for (i = 0; i < DV3D_OBJ_COUNT; i++)
		fx.fail_alloc[i] = false;
	fx.fail_map = fx.fail_create = fx.fail_load = -1;
	fx.short_load = fx.split_load = fx.misalign_obj = -1;
	fx.above4g_obj = fx.overlap_obj = -1;
	reg.mask_sts = 0x0000007f;
	reg.mmu_debug = 0x20804664;
	reg.tfu_cs = 0x100;	/* idle, one free slot hint, counter 0 */
	fake_takeover_complete = true;
	poke_count = 0;
	mmio_pokes = 0;
	peek_counter = 0;
	fail_peek_at = fail_poke_at = 0;
	pre_syncs_before_publication = 0;
	publication_seen = false;
	allocs = maps = creates = loads = 0;
	frees = unmaps = destroys = unloads = 0;
	inside_driver = false;
}

static int
run_case(const char *name, int expected)
{
	int error;

	case_name = name;
	cases++;
	inside_driver = true;
	error = bcmv3d_dma_probe(&dev, &tag);
	inside_driver = false;
	CHECK(error == expected);
	return error;
}

static void
assert_retained(void)
{
	int i;

	CHECK(dv3d.published);
	/* Nothing published to the GPU may be released, even after failure. */
	CHECK(frees == 0 && unmaps == 0 && destroys == 0 && unloads == 0);
	for (i = 0; i < DV3D_OBJ_COUNT; i++)
		CHECK(dv3d.obj[i].allocated && allocations[i].live);
}

static void
assert_released(void)
{

	CHECK(!dv3d.published);
	CHECK(frees == allocs && unmaps == maps && destroys == creates &&
	    unloads == loads);
}

static void
check_pte_permissions(void)
{
	struct fake_allocation *pt = allocation_by_base(
	    (bus_addr_t)reg.pt_base << DV3D_PAGE_SHIFT);
	bus_addr_t alias_src = allocations[DV3D_OBJ_ALIAS_SRC].base;
	bus_addr_t alias_dst = allocations[DV3D_OBJ_ALIAS_DST].base;
	uint32_t actual_src = allocations[DV3D_OBJ_ACTUAL_SRC].base >> 12;
	uint32_t actual_dst = allocations[DV3D_OBJ_ACTUAL_DST].base >> 12;
	uint32_t pte;
	int i;

	assert(pt != NULL);
	for (i = 0; i < DV3D_IMAGE_PAGES; i++) {
		/* Sources stay read-only; destinations are writable; no big
		 * or super pages appear anywhere in the used entries. */
		memcpy(&pte, pt->dev + ((alias_src >> 12) + i) * 4, 4);
		CHECK((pte & 0x00ffffff) == actual_src + (uint32_t)i);
		CHECK((pte & (DV3D_PTE_VALID | DV3D_PTE_WRITEABLE)) ==
		    DV3D_PTE_VALID);
		memcpy(&pte, pt->dev + ((alias_dst >> 12) + i) * 4, 4);
		CHECK((pte & 0x00ffffff) == actual_dst + (uint32_t)i);
		CHECK((pte & (DV3D_PTE_VALID | DV3D_PTE_WRITEABLE)) ==
		    (DV3D_PTE_VALID | DV3D_PTE_WRITEABLE));
	}
}

int
main(void)
{
	int i;

	/* Without a completed takeover there is no DMA experiment. */
	reset_fixture();
	fake_takeover_complete = false;
	run_case("no completed takeover", EPERM);

	/* The translated copy passes once, and only once per boot. */
	reset_fixture();
	run_case("translated copy", 0);
	run_case("second attempt", EBUSY);
	CHECK(strstr(fx.last_message, "translated DMA PASS") != NULL);
	assert_retained();
	/* All six pre-syncs precede the publishing PT base write. */
	CHECK(pre_syncs_before_publication >= 6);
	CHECK(poke_order[0] == R_PT_BASE);
	/* ICFG starts the copy and is the last register written. */
	CHECK(poke_order[poke_count - 1] == R_TFU_ICFG);
	/* The pinned TFU submission order from v3d_sched.c. */
	CHECK(poke_order[poke_count - 12] == R_TFU_IIA);
	CHECK(poke_order[poke_count - 11] == R_TFU_IIS);
	CHECK(poke_order[poke_count - 10] == R_TFU_ICA);
	CHECK(poke_order[poke_count - 9] == R_TFU_IUA);
	CHECK(poke_order[poke_count - 8] == R_TFU_IOA);
	CHECK(poke_order[poke_count - 7] == R_TFU_IOC);
	CHECK(poke_order[poke_count - 6] == R_TFU_IOS);
	CHECK(poke_order[poke_count - 5] == R_TFU_COEF0);
	check_pte_permissions();

	/* Counter wrap: 0xff + 1 completes at 0 modulo 256. */
	reset_fixture();
	reg.tfu_cs = UINT32_C(0x00ff0100);
	run_case("counter wrap", 0);

	/* Identity addressing is a distinct outcome, never PASS. */
	reset_fixture();
	fx.mode = GPU_IDENTITY;
	run_case("identity addressing", EIO);
	CHECK(strstr(fx.last_message, "identity") != NULL);
	assert_retained();

	/* Mixed translated and identity data never counts as PASS. */
	reset_fixture();
	fx.mode = GPU_MIXED;
	run_case("mixed destinations", EIO);
	assert_retained();

	/* Missing data in the actual destination fails verification. */
	reset_fixture();
	fx.mode = GPU_GARBAGE;
	run_case("garbage copy", EIO);
	assert_retained();

	/* A GPU write into a protected source is detected after sync. */
	reset_fixture();
	fx.mode = GPU_TOUCH_SOURCE;
	run_case("source touched", EIO);
	assert_retained();

	/* A scratch modification is detected. */
	reset_fixture();
	fx.mode = GPU_TOUCH_SCRATCH;
	run_case("scratch touched", EIO);
	assert_retained();

	/* A fault wins over simultaneous completion. */
	reset_fixture();
	fx.mode = GPU_FAULT;
	run_case("fault beats completion", EFAULT);
	assert_retained();

	/* No completion within the bounded watch. */
	reset_fixture();
	fx.mode = GPU_TIMEOUT;
	run_case("bounded timeout", ETIMEDOUT);
	assert_retained();
	CHECK(fx.delays > 0);

	/* Double counter movement is not exactly-once completion. */
	reset_fixture();
	fx.mode = GPU_DOUBLE;
	run_case("double counter", ETIMEDOUT);
	assert_retained();

	/* Stuck cache and TLB clearing waits are bounded. */
	reset_fixture();
	fx.flush_stuck = true;
	run_case("MMUC flush stuck", ETIMEDOUT);
	assert_retained();

	reset_fixture();
	fx.tlb_stuck = true;
	run_case("TLB clear stuck", ETIMEDOUT);
	assert_retained();

	/* Unsuitable MMU geometry and mask regression. */
	reset_fixture();
	reg.mmu_debug = 0x20804644;
	run_case("narrow VA", EOPNOTSUPP);

	reset_fixture();
	reg.mmu_debug = 0x20804364;
	run_case("narrow PA", EOPNOTSUPP);

	reset_fixture();
	reg.mask_sts = 0;
	run_case("unmasked interrupts", EIO);

	/* Every allocation failure releases cleanly before exposure. */
	for (i = 0; i < DV3D_OBJ_COUNT; i++) {
		reset_fixture();
		fx.fail_alloc[i] = true;
		run_case("allocation failure", ENOMEM);
		assert_released();
	}
	reset_fixture();
	fx.fail_map = DV3D_OBJ_PT;
	run_case("mapping failure", ENOMEM);
	assert_released();

	reset_fixture();
	fx.fail_create = DV3D_OBJ_SCRATCH;
	run_case("map create failure", ENOMEM);
	assert_released();

	reset_fixture();
	fx.fail_load = DV3D_OBJ_ALIAS_DST;
	run_case("map load failure", ENOMEM);
	assert_released();

	/* Rejected loaded layouts. */
	reset_fixture();
	fx.split_load = DV3D_OBJ_ACTUAL_DST;
	run_case("split segment", EIO);
	assert_released();

	reset_fixture();
	fx.short_load = DV3D_OBJ_ACTUAL_SRC;
	run_case("short segment", EIO);
	assert_released();

	reset_fixture();
	fx.misalign_obj = DV3D_OBJ_SCRATCH;
	run_case("misaligned address", EIO);
	assert_released();

	reset_fixture();
	fx.above4g_obj = DV3D_OBJ_ALIAS_SRC;
	run_case("above 4 GiB", EIO);
	assert_released();

	reset_fixture();
	fx.overlap_obj = DV3D_OBJ_ALIAS_DST;
	run_case("overlapping ranges", EIO);
	assert_released();

	/* MMIO faults at decisive points. */
	reset_fixture();
	fail_peek_at = 1;
	run_case("debug read fault", EIO);

	reset_fixture();
	fail_poke_at = 1;
	run_case("PT base write fault", EIO);
	assert_retained();

	reset_fixture();
	fail_poke_at = 4;	/* the MMUC enable write */
	run_case("MMUC write fault", EIO);
	assert_retained();

	reset_fixture();
	fail_poke_at = 10;	/* inside the TFU submission */
	run_case("TFU write fault", EIO);
	assert_retained();

	printf("PASS: %u cases, %u checks\n", cases, checks);
	return 0;
}
