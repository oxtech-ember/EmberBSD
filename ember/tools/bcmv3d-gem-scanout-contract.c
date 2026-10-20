/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual BO allocator/reference, backing, frame helper and table bodies. */
#define _BUS_DMAMAP_IS_BOUNCING 0x20000
#include "bo-services.h"
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include "bcmv3d_hvs_prepare.h"
#include "bcmv3d_mmu4_backing.h"
#include "bcm2712_mmu4_corevar.h"
#include "bcm2712_mmu4_tablevar.h"

#define atomic_load_relaxed(p) __atomic_load_n((p), __ATOMIC_RELAXED)
#define atomic_store_relaxed(p, v) __atomic_store_n((p), (v), __ATOMIC_RELAXED)
#define BUS_DMASYNC_POSTWRITE 8
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define htole32(v) ((uint32_t)(v))
#define le32toh(v) ((uint32_t)(v))
#else
#define htole32(v) __builtin_bswap32((uint32_t)(v))
#define le32toh(v) __builtin_bswap32((uint32_t)(v))
#endif
#define BUS_DMA_WRITE 0x200
#define BUS_DMASYNC_PREWRITE 4
static unsigned intr, syncs, pmap_calls, fail_pmap, bad_last;
static bool coherent_value;
#define ASSERT_SLEEPABLE() CHECK(intr == 0)
static int cpu_intr_p(void) { return intr == 1; }
static int cpu_softintr_p(void) { return intr == 2; }
static void *pmap_kernel(void) { return &intr; }

static bool
pmap_extract_coherency(void *pm, vaddr_t va, paddr_t *pa, bool *coherent)
{
	uint64_t offset;
	unsigned int i;
	int j;

	CHECK(pm == &intr);
	if (++pmap_calls == fail_pmap)
		return false;
	for (i = 0; i < 16; i++) {
		if (dma_segments[i].memory == NULL ||
		    va < (uintptr_t)dma_segments[i].memory)
			continue;
		offset = va - (uintptr_t)dma_segments[i].memory;
		for (j = 0; j < dma_counts[i]; j++) {
			if (offset < dma_lists[i][j].ds_len) {
				*pa = dma_lists[i][j].ds_addr + offset;
				if (bad_last && (offset & 4095) == 4095)
					(*pa)++;
				*coherent = coherent_value;
				return true;
			}
			offset -= dma_lists[i][j].ds_len;
		}
	}
	CHECK(false);
	return false;
}

static void
bus_dmamap_sync(bus_dma_tag_t tag, bus_dmamap_t map, bus_addr_t offset,
    bus_size_t size, int flags)
{
	CHECK(map != NULL && offset == 0 && size == map->dm_mapsize);
	CHECK(flags == BUS_DMASYNC_PREWRITE);
	syncs++;
}
#include "bo-body.h"
#include "hvs-body.h"
#include "table-body.h"
#include "backing-body.h"

static struct drm_device drm;
static struct bcmv3d_device device;
static struct drm_file file;
static struct bcmv3d_mmu4_backing *backing;
static struct bcm2712_mmu4_config saved_cfg;
static struct bcm2712_mmu4_pages saved_pages;
static struct bcm2712_mmu4_image saved_image;
static struct bcmv3d_mmu4_backing_status saved_status;
static void *saved_frame, *saved_control[4];

static uint32_t
load32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	    ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void
initial_unchanged(void)
{
	unsigned int i;

	CHECK(memcmp(&saved_cfg, &backing->cfg, sizeof(saved_cfg)) == 0);
	CHECK(memcmp(&saved_pages, &backing->pages, sizeof(saved_pages)) == 0);
	CHECK(memcmp(&saved_image, &backing->image, sizeof(saved_image)) == 0);
	CHECK(memcmp(&saved_status, &backing->status, sizeof(saved_status)) == 0);
	CHECK(memcmp(saved_frame, backing->frame->vaddr,
	    backing->status.frame_bytes) == 0);
	for (i = 0; i < 4; i++)
		CHECK(memcmp(saved_control[i], backing->control[i].kva,
		    backing->status.control_bytes) == 0);
	CHECK(syncs == 5);
}

static struct bcmv3d_bo *
user_bo(void)
{
	struct drm_v3d_create_bo args = { .size = BCMV3D_HVS_BYTES };

	CHECK(bcmv3d_create_bo(&drm, &args, &file) == 0);
	CHECK(args.handle != 0);
	return bcmv3d_to_bo(file.handles[args.handle]);
}

static void
initial(void)
{
	unsigned int i;

	device.drm = &drm;
	device.dmat = &device;
	device.bo_total = BCMV3D_BO_TOTAL_MIN;
	drm.dev_private = &device;
	CHECK(bcmv3d_mmu4_backing_create(&device, 0x20804774, &backing) == 0);
	CHECK(bcmv3d_mmu4_backing_get_config(backing, &saved_cfg) == 0);
	CHECK(bcmv3d_mmu4_backing_prepare(backing, &saved_cfg) == 0);
	saved_cfg = backing->cfg;
	saved_pages = backing->pages;
	saved_image = backing->image;
	saved_status = backing->status;
	saved_frame = malloc(backing->status.frame_bytes);
	CHECK(saved_frame != NULL);
	memcpy(saved_frame, backing->frame->vaddr, backing->status.frame_bytes);
	for (i = 0; i < 4; i++) {
		saved_control[i] = malloc(backing->status.control_bytes);
		CHECK(saved_control[i] != NULL);
		memcpy(saved_control[i], backing->control[i].kva,
		    backing->status.control_bytes);
	}
}

static void
slot_image(unsigned int slot, struct bcmv3d_bo *bo)
{
	struct mmu4_gem_slot *g = backing->staged[slot];
	uint64_t pa, offset;
	unsigned int i, seg = 0;
	uint32_t expected;

	CHECK(g != NULL && g->bo == bo);
	CHECK(g->map != bo->cma.dmamap && g->map->dm_nsegs > 1);
	CHECK(memcmp(backing->candidate_top, saved_image.top, 8) == 0);
	CHECK(load32(backing->candidate_top + (slot + 1) * 8) ==
	    (0x10000000 | (uint32_t)(g->control[0].pa >> 12)));
	CHECK(load32(backing->candidate_top + (slot + 1) * 8 + 4) ==
	    (0x10000000 | (uint32_t)(g->control[1].pa >> 12)));
	offset = 0;
	for (i = 0; i < 2048; i++) {
		expected = 0;
		if (i < 2025) {
			pa = bo->cma.dmamap->dm_segs[seg].ds_addr + offset;
			expected = 0x10000000 | (uint32_t)(pa >> 12);
			offset += 4096;
			if (offset == bo->cma.dmamap->dm_segs[seg].ds_len) {
				seg++;
				offset = 0;
			}
		}
		CHECK(load32((uint8_t *)g->control[i / 1024].kva +
		    (i % 1024) * 4) == expected);
	}
	for (i = 0; i < 2; i++) {
		for (offset = 4096; offset < PAGE_SIZE; offset++)
			CHECK(((uint8_t *)g->control[i].kva)[offset] == 0);
	}
	/* Preserve every unrelated candidate entry, including both boot entries. */
	for (i = 0; i < 1024; i++) {
		expected = load32(saved_image.top + i * 4);
		if (i >= 2 && i < 8 && backing->staged[i / 2 - 1] != NULL) {
			pa = backing->staged[i / 2 - 1]->control[i % 2].pa;
			expected = 0x10000000 | (uint32_t)(pa >> 12);
		}
		CHECK(load32(backing->candidate_top + i * 4) == expected);
	}
	initial_unchanged();
}

static void
stage_failure(struct bcmv3d_bo *bo, int expected)
{
	uint8_t top[4096];
	unsigned int refs = bo->cma.base.refs;
	unsigned int am = allocated, mm = maps, lm = loaded, km = kmem_blocks;
	size_t quota = device.allocated;
	unsigned int i;

	memcpy(top, backing->candidate_top, sizeof(top));
	CHECK(bcmv3d_mmu4_backing_stage_gem(backing, 0, &bo->cma.base) == expected);
	CHECK(backing->staged[0] == NULL && !backing->running);
	CHECK(bo->cma.base.refs == refs && device.allocated == quota);
	CHECK(allocated == am && maps == mm && loaded == lm && kmem_blocks == km);
	CHECK(memcmp(top, backing->candidate_top, sizeof(top)) == 0);
	for (i = 0; i < 3; i++) {
		if (backing->staged[i] != NULL)
			slot_image(i, backing->staged[i]->bo);
	}
	initial_unchanged();
}

static void
range_errors(void)
{
	static struct bcm2712_mmu4_range_pages pages;
	static struct bcm2712_mmu4_range_image out, saved;
	unsigned int i;

	pages.debug = 0x20804774;
	pages.top_pa = 0x01000000;
	pages.l2_pa[0] = 0x01001000;
	pages.l2_pa[1] = 0x01002000;
	for (i = 0; i < 2025; i++)
		pages.frame_pa[i] = UINT64_C(0x02000000) + i * 4096;
	memset(&out, 0xa5, sizeof(out));
	saved = out;
	CHECK(bcm2712_mmu4_encode_range(&pages, 3, &out) == EINVAL);
	CHECK(memcmp(&saved, &out, sizeof(out)) == 0);
	pages.frame_pa[2024] = pages.l2_pa[1];
	CHECK(bcm2712_mmu4_encode_range(&pages, 0, &out) == EINVAL);
	CHECK(memcmp(&saved, &out, sizeof(out)) == 0);
	pages.frame_pa[2024] = UINT64_C(1) << 36;
	CHECK(bcm2712_mmu4_encode_range(&pages, 0, &out) == ERANGE);
	CHECK(memcmp(&saved, &out, sizeof(out)) == 0);
	pages.frame_pa[2024] = (UINT64_C(1) << 36) - 4096;
	CHECK(bcm2712_mmu4_encode_range(&pages, 2, &out) == 0);
	CHECK(load32(out.top) == 0x10001001);
	CHECK(load32(out.top + 4) == 0x10001002);
	CHECK(load32(out.l2[1] + 1000 * 4) == 0x10ffffff);
	CHECK(load32(out.l2[1] + 1001 * 4) == 0);
	CHECK(bcm2712_mmu4_encode_range(&pages, 0,
	    (struct bcm2712_mmu4_range_image *)&pages) == EINVAL);
}

static void
sealed_discard(struct bcmv3d_bo *bo)
{
	pid_t pid;
	int status;

	pid = fork();
	CHECK(pid >= 0);
	if (pid == 0) {
		CHECK(bcmv3d_mmu4_backing_seal(backing) == 0);
		saved_status = backing->status;
		CHECK(bcmv3d_mmu4_backing_stage_gem(backing, 0, &bo->cma.base) == 0);
		CHECK(bcmv3d_mmu4_backing_discard_staged_gem(backing, 0) == 0);
		initial_unchanged();
		CHECK(bcmv3d_mmu4_backing_destroy_unexposed(backing) == EBUSY);
		/* Sealed initial backing survives until the isolated boot exits. */
		_Exit(0);
	}
	CHECK(waitpid(pid, &status, 0) == pid);
	CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

int
main(void)
{
	struct bcmv3d_bo *bo[3];
	struct bcmv3d_bo *out;
	struct drm_gem_object fake = { .dev = &drm, .refs = 1 };
	unsigned int i, before, pmap_steps, service_steps, refs;
	uint64_t saved_dma;
	size_t quota;
	const char *control = getenv("SCANOUT_HARNESS_CONTROL");

	if (control != NULL) {
		if (strcmp(control, "segv") == 0)
			raise(SIGSEGV);
		if (strcmp(control, "abort") == 0)
			abort();
		if (strcmp(control, "hang") == 0) {
			for (;;)
				pause();
		}
		CHECK(false);
	}
	range_errors();

	initial();
	fragment_pages = 3;
	for (i = 0; i < 3; i++)
		bo[i] = user_bo();
	quota = device.allocated;
	sealed_discard(bo[0]);
	out = bo[1];
	CHECK(bcmv3d_bo_reference(&device, &fake, &out) == ENXIO && out == bo[1]);
	CHECK(fake.refs == 1);
	fake.funcs = &bcmv3d_bo_funcs;
	fake.dev = NULL;
	CHECK(bcmv3d_bo_reference(&device, &fake, &out) == ENXIO && out == bo[1]);
	CHECK(fake.refs == 1);

	refs = bo[0]->cma.base.refs;
	CHECK(bcmv3d_bo_reference(&device, &bo[0]->cma.base, &out) == 0);
	CHECK(out == bo[0] && bo[0]->cma.base.refs == refs + 1);
	drm_gem_object_put_unlocked(&out->cma.base);
	for (i = 1; i <= 2; i++) {
		intr = i;
		stage_failure(bo[0], EWOULDBLOCK);
	}
	intr = 0;
	bo[0]->cma.dmamap->_dm_flags = _BUS_DMAMAP_IS_BOUNCING;
	stage_failure(bo[0], EOPNOTSUPP);
	bo[0]->cma.dmamap->_dm_flags = 0;
	bo[0]->cma.dmamap->_dm_iommu = &fake;
	stage_failure(bo[0], EOPNOTSUPP);
	bo[0]->cma.dmamap->_dm_iommu = NULL;
	saved_dma = bo[0]->cma.dmamap->dm_mapsize;
	bo[0]->cma.dmamap->dm_mapsize--;
	stage_failure(bo[0], EINVAL);
	bo[0]->cma.dmamap->dm_mapsize = saved_dma;
	saved_dma = bo[0]->cma.dmamap->dm_segs[0].ds_addr;
	bo[0]->cma.dmamap->dm_segs[0].ds_addr += 4096;
	stage_failure(bo[0], EOPNOTSUPP);
	bo[0]->cma.dmamap->dm_segs[0].ds_addr = saved_dma;

	bad_last = 1;
	stage_failure(bo[0], EAGAIN);
	bad_last = 0;
	allocation_alias = backing->control[3].pa;
	stage_failure(bo[0], EINVAL);
	allocation_alias = 0;
	for (i = 1; i <= 3; i++) {
		bad_load_call = service_calls + 2;
		load_fault = i;
		stage_failure(bo[0], EOPNOTSUPP);
		bad_load_call = load_fault = 0;
	}
	bad_load_call = service_calls + 6;
	load_fault = 1;
	stage_failure(bo[0], EOPNOTSUPP);
	bad_load_call = load_fault = 0;
	split_maps = coherent_value = true;
	CHECK(bcmv3d_mmu4_backing_stage_gem(backing, 0, &bo[0]->cma.base) == 0);
	CHECK(backing->staged[0]->map->dm_nsegs > bo[0]->cma.dmamap->dm_nsegs);
	slot_image(0, bo[0]);
	CHECK(bcmv3d_mmu4_backing_stage_gem(backing, 1, &bo[0]->cma.base) == EEXIST);
	CHECK(bcmv3d_mmu4_backing_discard_staged_gem(backing, 0) == 0);
	split_maps = coherent_value = false;
	before = service_calls;

	pmap_steps = pmap_calls;
	CHECK(bcmv3d_mmu4_backing_stage_gem(backing, 0, &bo[0]->cma.base) == 0);
	service_steps = service_calls - before;
	pmap_steps = pmap_calls - pmap_steps;
	CHECK(pmap_steps == (PAGE_SIZE == 4096 ? 4062U : 4256U));
	slot_image(0, bo[0]);
	CHECK(bcmv3d_mmu4_backing_discard_staged_gem(backing, 0) == 0);
	for (i = 1; i <= service_steps; i++) {
		fail_service = service_calls + i;
		stage_failure(bo[0], EIO);
		fail_service = 0;
	}
	for (i = 1; i <= pmap_steps; i++) {
		if (getenv("SCANOUT_SHORT") != NULL && i != 1 && i != pmap_steps)
			continue;
		fail_pmap = pmap_calls + i;
		stage_failure(bo[0], EFAULT);
		fail_pmap = 0;
	}
	for (i = 0; i < 3; i++) {
		refs = bo[i]->cma.base.refs;
		bo[i]->busy = 1; /* Page proof is independent of rendering readiness. */
		CHECK(bcmv3d_mmu4_backing_stage_gem(backing, i, &bo[i]->cma.base) == 0);
		CHECK(bo[i]->cma.base.refs == refs + 1);
		CHECK(device.allocated == quota);
		slot_image(i, bo[i]);
		bo[i]->busy = 0;
	}
	before = service_calls;
	CHECK(bcmv3d_mmu4_backing_stage_gem(backing, 0, &bo[1]->cma.base) == EBUSY);
	CHECK(bcmv3d_mmu4_backing_stage_gem(backing, 3, &bo[0]->cma.base) == EINVAL);
	CHECK(service_calls == before);
	CHECK(bcmv3d_mmu4_backing_discard_staged_gem(backing, 0) == 0);
	/* A failed new slot must preserve the two already committed slots. */
	fail_service = service_calls + service_steps;
	stage_failure(bo[0], EIO);
	fail_service = 0;
	fail_pmap = pmap_calls + pmap_steps;
	stage_failure(bo[0], EFAULT);
	fail_pmap = 0;
	CHECK(bcmv3d_mmu4_backing_stage_gem(backing, 0, &bo[0]->cma.base) == 0);
	for (i = 0; i < 3; i++) {
		CHECK(bcmv3d_mmu4_backing_discard_staged_gem(backing, i) == 0);
		CHECK(load32(backing->candidate_top + (i + 1) * 8) == 0);
		CHECK(load32(backing->candidate_top + (i + 1) * 8 + 4) == 0);
		CHECK(bcmv3d_mmu4_backing_discard_staged_gem(backing, i) == ENOENT);
	}
	initial_unchanged();
	CHECK(bcmv3d_mmu4_backing_stage_gem(backing, 1, &bo[0]->cma.base) == 0);
	/* Drop the user handle. Only the real staging reference retains this BO. */
	for (i = 1; i < 16; i++) {
		if (file.handles[i] == &bo[0]->cma.base) {
			drm_gem_object_put_unlocked(file.handles[i]);
			file.handles[i] = NULL;
		}
	}
	CHECK(backing->staged[1]->bo->cma.base.refs == 1);
	CHECK(bcmv3d_mmu4_backing_destroy_unexposed(backing) == 0);
	for (i = 1; i < 16; i++) {
		if (file.handles[i] != NULL)
			drm_gem_object_put_unlocked(file.handles[i]);
	}
	for (i = 0; i < 4; i++)
		free(saved_control[i]);
	free(saved_frame);
	CHECK(device.allocated == 0 && allocated == 0 && mapped == 0);
	CHECK(maps == 0 && loaded == 0 && ranges == 0 && gems == 0 && kmem_blocks == 0);
	printf("PASS GEM scanout PAGE_SIZE=%u, %u service and %u pmap fault positions, %u checks\n",
	    PAGE_SIZE, service_steps, pmap_steps, checks);
	return 0;
}
