/* Origin: EmberBSD actual-source fragmented BO and UVM/PTE contract. */
/* SPDX-License-Identifier: BSD-2-Clause */
#define BCMV3D_JOB_CONTRACT
#include "bcm2712-v3d-bo-contract.c"
#define V3D_COPY_SIZE (64 * 64 * 4)
#define V3D_PTE_VALID (1U << 28)
#define V3D_PTE_WRITE (1U << 29)
#define BUS_DMASYNC_PREWRITE 1
#ifndef __arraycount
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#endif
struct bcmv3d_job {
	struct bcmv3d_bo *pt, *src, *dst, *bos[2];
	unsigned nbo;
};
static struct bcmv3d_bo *expected_sync;
static unsigned sync_count;
static void
bcmv3d_sync(const struct bcmv3d_bo *bo, int ops)
{
	CHECK(bo == expected_sync && ops == BUS_DMASYNC_PREWRITE);
	sync_count++;
}
#include "ptes-body.h"

static struct bcmv3d_bo *
create(struct drm_device *d, struct drm_file *f, unsigned pages, unsigned *handle)
{
	struct drm_v3d_create_bo a = { .size = pages * PAGE_SIZE };
	int error = bcmv3d_create_bo(d, &a, f);

	CHECK(error == 0 && a.handle != 0);
	*handle = a.handle;
	return bcmv3d_to_bo(f->handles[a.handle]);
}

static void
allocation(struct bcmv3d_device *c, struct drm_device *d, struct drm_file *f)
{
	struct drm_v3d_create_bo a = { .size = 7475200 };
	struct bcmv3d_bo *bo;
	int error;

	/* Original one-segment user allocation fails in this memory model. */
	contiguous_limit = 5246976;
	fragment_pages = 3;
	error = bcmv3d_create_bo(d, &a, f);
	CHECK(error == 0 && a.handle != 0);
	bo = bcmv3d_to_bo(f->handles[a.handle]);
	CHECK(bo->nsegs > 1 && bo->cma.dmamap->dm_nsegs > 1);
	CHECK(bo->maxsegs == 1825 && c->allocated == 7475200);
	close_handle(f, a.handle);
	empty(c);
	bo = bcmv3d_bo_alloc(c, 7475200);
	CHECK(IS_ERR(bo) && PTR_ERR(bo) == -ENOMEM);
	empty(c);
	bo = bcmv3d_bo_alloc(c, 4 * 1024 * 1024);
	CHECK(!IS_ERR(bo));
	CHECK(bo->nsegs == 1 && bo->cma.dmamap->dm_nsegs == 1);
	drm_gem_object_put_unlocked(&bo->cma.base);
	empty(c);
	contiguous_limit = 0;
	for (fail = 1; fail <= 6; fail++) {
		a = (struct drm_v3d_create_bo){ .size = 6 * PAGE_SIZE };
		error = bcmv3d_create_bo(d, &a, f);
		CHECK(error < 0 && a.handle == 0 && a.offset == 0);
		empty(c);
	}
	fail = 0;
	for (bad_geometry = 1; bad_geometry <= 10; bad_geometry++) {
		a = (struct drm_v3d_create_bo){ .size = 6 * PAGE_SIZE };
		error = bcmv3d_create_bo(d, &a, f);
		CHECK(error == -EINVAL && a.handle == 0 && a.offset == 0);
		empty(c);
	}
	bad_geometry = 0;
}

static void
mapping(const struct bcmv3d_device *c, struct drm_device *d, struct drm_file *f)
{
	unsigned h;
	struct bcmv3d_bo *bo = create(d, f, 5, &h);
	struct uvm_object *u = &bo->cma.base.gemo_uvmobj;
	struct vm_map_entry entry = {
		.object = { u }, .start = 0x10000, .protection = 3
	};
	struct vm_map map = {0};
	struct uvm_faultinfo fault = { &entry, &map };
	struct vm_page *pages[5] = {0};
	const struct uvm_pagerops *ops = &bcmv3d_bo_uvm_ops;
	int error;

	CHECK(bo->nsegs == 2 && bo->dmasegs[0].ds_len == 3 * PAGE_SIZE);
	CHECK(ops->pgo_reference == drm_gem_pager_reference);
	CHECK(ops->pgo_detach == drm_gem_pager_detach);
	ops->pgo_reference(u);
	close_handle(f, h);
	CHECK(gems == 1 && c->allocated == 5 * PAGE_SIZE);
	pager_maps = pager_updates = pager_unlocks = 0;
	error = ops->pgo_fault(&fault, entry.start, pages, 5, 0, 3, PGO_ALLPAGES);
	CHECK(error == 0 && pager_maps == 5);
	CHECK(pager_updates == 1 && pager_unlocks == 1);
	CHECK(pager_addresses[2] == bo->dmasegs[0].ds_addr + 2 * PAGE_SIZE);
	CHECK(pager_addresses[3] == bo->dmasegs[1].ds_addr);
	CHECK(pager_addresses[4] == bo->dmasegs[1].ds_addr + PAGE_SIZE);
	pager_maps = 0;
	error = ops->pgo_fault(&fault, entry.start, pages, 5, 3, 3, 0);
	CHECK(error == 0 && pager_maps == 1);
	CHECK(pager_addresses[0] == bo->dmasegs[1].ds_addr);
	pages[3] = PGO_DONTCARE; pager_maps = 0;
	error = ops->pgo_fault(&fault, entry.start, pages, 5, 3, 3, 0);
	CHECK(error == 0 && pager_maps == 0);
	pages[3] = NULL;
	entry.offset = 3 * PAGE_SIZE;
	error = ops->pgo_fault(&fault, entry.start, pages, 2, 0, 3, PGO_ALLPAGES);
	CHECK(error == 0 && pager_maps == 2);
	CHECK(pager_addresses[0] == bo->dmasegs[1].ds_addr);
	for (unsigned mode = 0; mode < 6; mode++) {
		unsigned before = pager_unlocks;
		entry.cow = mode == 0;
		entry.offset = mode == 1 ? -PAGE_SIZE : mode == 2 ? 5 * PAGE_SIZE : 0;
		pager_fail = mode == 3 ? 1 : mode == 4 ? 2 : 0;
		pager_maps = 0;
		error = ops->pgo_fault(&fault, mode == 5 ? entry.start - PAGE_SIZE : entry.start,
		    pages, 1, 0, 3, 0);
		CHECK(error == (mode == 4 ? ENOMEM : EIO));
		CHECK(pager_unlocks == before + 1 && pager_maps == 0);
	}
	pager_fail = 0;
	ops->pgo_detach(u);
	empty(c);
}

static void
ptes(struct bcmv3d_device *c, struct drm_device *d, struct drm_file *f)
{
	unsigned hs, hd;
	struct bcmv3d_bo *src = create(d, f, 6, &hs);
	struct bcmv3d_bo *dst = create(d, f, 6, &hd);
	struct bcmv3d_bo *pt = bcmv3d_bo_alloc(c, 4 * 1024 * 1024);
	struct bcmv3d_job job = { .src = src, .dst = dst, .pt = pt };
	uint32_t *table;
	unsigned si = (unsigned)(src->gpu / PAGE_SIZE);
	unsigned di = (unsigned)(dst->gpu / PAGE_SIZE);

	CHECK(!IS_ERR(pt)); table = pt->cma.vaddr; expected_sync = pt;
	/* A translated bus map must drive PTEs; CPU mmap uses allocation segments. */
	for (int i = 0; i < src->cma.dmamap->dm_nsegs; i++)
		src->cma.dmamap->dm_segs[i].ds_addr += 0x10000000;
	CHECK(src->cma.dmamap->dm_nsegs == 2);
	table[si + 4] = table[di + 4] = 0x12345678;
	bcmv3d_ptes(&job, true);
	CHECK(table[si] == ((src->dmasegs[0].ds_addr + 0x10000000) / PAGE_SIZE | V3D_PTE_VALID));
	CHECK(table[si + 3] == ((src->dmasegs[1].ds_addr + 0x10000000) / PAGE_SIZE | V3D_PTE_VALID));
	CHECK(table[di + 3] == (dst->dmasegs[1].ds_addr / PAGE_SIZE | V3D_PTE_VALID | V3D_PTE_WRITE));
	CHECK(table[si + 4] == 0x12345678 && table[di + 4] == 0x12345678);
	bcmv3d_ptes(&job, false);
	for (unsigned i = 0; i < 4; i++) CHECK(table[si + i] == 0 && table[di + i] == 0);
	CHECK(table[si + 4] == 0x12345678 && table[di + 4] == 0x12345678);
	job.nbo = 1; job.bos[0] = src;
	bcmv3d_ptes(&job, true);
	CHECK(table[si + 5] == (((src->dmasegs[1].ds_addr + 0x10000000) / PAGE_SIZE + 2) | V3D_PTE_VALID | V3D_PTE_WRITE));
	bcmv3d_ptes(&job, false);
	for (unsigned i = 0; i < 6; i++) CHECK(table[si + i] == 0);
	CHECK(sync_count == 4);
	close_handle(f, hs); close_handle(f, hd);
	drm_gem_object_put_unlocked(&pt->cma.base);
	expected_sync = NULL;
	empty(c);
}

int
main(void)
{
	struct bcmv3d_device c = {0};
	struct drm_device d = { .dev_private = &c };
	struct drm_file f = {0};

	c.drm = &d;
	allocation(&c, &d, &f);
	mapping(&c, &d, &f);
	ptes(&c, &d, &f);
	CHECK(fragmented_allocations > 10);
	printf("PASS: %u actual-source SG allocation, pager and PTE checks\n", checks);
	return 0;
}
