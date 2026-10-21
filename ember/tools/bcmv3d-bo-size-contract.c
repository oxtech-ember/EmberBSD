/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Actual native allocator, mmap and PTE bodies with controlled services. */
#include <sys/mman.h>
#define BCMV3D_JOB_CONTRACT
#include "bcm2712-v3d-bo-contract.c"

struct bcmv3d_job {
	struct bcmv3d_bo *pt, *src, *dst;
	struct bcmv3d_bo *tfu_bos[4];
	unsigned int ntfu;
	struct drm_gem_object *bos[1];
	unsigned int nbo;
};
#ifndef __arraycount
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#endif
#define BUS_DMASYNC_PREWRITE 1
static unsigned int pt_syncs;
static void
bcmv3d_sync(struct bcmv3d_bo *bo, int flags)
{
	CHECK(bo != NULL && bo->cma.vaddr != NULL);
	CHECK(flags == BUS_DMASYNC_PREWRITE);
	pt_syncs++;
}
#include "size-body.h"

static size_t
rounded(uint32_t size)
{
	return ((size_t)size + PAGE_SIZE - 1) & ~((size_t)PAGE_SIZE - 1);
}

static void
check_mmap(struct drm_gem_object *obj, struct bcmv3d_bo *bo)
{
	struct uvm_object *uobj = NULL;
	struct vm_map_entry entry = { .object.uvm_obj = &obj->gemo_uvmobj,
	    .start = 0x10000000, .protection = 3 };
	struct vm_map map = { 0 };
	struct uvm_faultinfo info = { &entry, &map };
	struct vm_page *pages[1] = { NULL };
	off_t offset = 0;
	int flags = MAP_SHARED, advice = 0, prot = 0;
	unsigned int refs = obj->refs;

	CHECK(bcmv3d_prime_mmap(obj, &offset, obj->size, 3, &flags,
	    &advice, &uobj, &prot) == 0);
	CHECK(obj->refs == refs + 1 && uobj == &obj->gemo_uvmobj && prot == 3);
	bcmv3d_bo_uvm_ops.pgo_detach(uobj);
	CHECK(obj->refs == refs);
	uobj = NULL;
	CHECK(bcmv3d_prime_mmap(obj, &offset, obj->size + PAGE_SIZE, 3,
	    &flags, &advice, &uobj, &prot) == -EINVAL);
	CHECK(uobj == NULL && obj->refs == refs);
	offset = (off_t)obj->size - PAGE_SIZE;
	CHECK(bcmv3d_prime_mmap(obj, &offset, PAGE_SIZE, 3, &flags,
	    &advice, &uobj, &prot) == 0);
	bcmv3d_bo_uvm_ops.pgo_detach(uobj);
	CHECK(obj->refs == refs);
	pager_maps = 0;
	CHECK(bcmv3d_bo_fault(&info, entry.start + obj->size - PAGE_SIZE,
	    pages, 1, 0, 3, PGO_ALLPAGES) == 0);
	CHECK(pager_maps == 1);
	CHECK(pager_addresses[0] == bo->dmasegs[bo->nsegs - 1].ds_addr +
	    bo->dmasegs[bo->nsegs - 1].ds_len - PAGE_SIZE);
	CHECK(bcmv3d_bo_fault(&info, entry.start + obj->size,
	    pages, 1, 0, 3, PGO_ALLPAGES) == EIO);
	CHECK(pager_maps == 1 && obj->refs == refs);
}

static void
check_ptes(struct bcmv3d_bo *bo)
{
	const uint32_t guard = UINT32_C(0xa55ac33c);
	const uint64_t end = BCMV3D_VA_START + BCMV3D_VA_SIZE;
	const size_t entries = V3D_PT_SIZE / sizeof(uint32_t);
	struct bcmv3d_bo pt = { 0 };
	struct bcmv3d_job job = { .pt = &pt, .bos = { &bo->cma.base }, .nbo = 1 };
	vmem_addr_t saved = bo->gpu;
	uint32_t *memory = malloc((entries + 2) * sizeof(*memory));
	uint32_t *table;
	size_t first, last, i;

	CHECK(PAGE_SIZE == 4096 && memory != NULL);
	CHECK(bo->cma.dmasize <= BCMV3D_BO_MAX && end <= UINT32_MAX);
	bo->gpu = end - bo->cma.dmasize;
	first = bo->gpu / 4096;
	last = end / 4096 - 1;
	CHECK(first <= last && last < entries);
	for (i = 0; i < entries + 2; i++)
		memory[i] = guard;
	pt.cma.vaddr = table = memory + 1;
	pt_syncs = 0;
	bcmv3d_ptes(&job, true);
	CHECK(pt_syncs == 1 && memory[0] == guard && memory[entries + 1] == guard);
	CHECK(table[first] == (bo->cma.dmamap->dm_segs[0].ds_addr / 4096 |
	    V3D_PTE_VALID | V3D_PTE_WRITE));
	CHECK(table[last] == ((bo->cma.dmamap->dm_segs[bo->cma.dmamap->dm_nsegs - 1].ds_addr +
	    bo->cma.dmamap->dm_segs[bo->cma.dmamap->dm_nsegs - 1].ds_len - 4096) / 4096 |
	    V3D_PTE_VALID | V3D_PTE_WRITE));
	for (i = 0; i < entries; i++) {
		if (i < first || i > last)
			CHECK(table[i] == guard);
		else
			CHECK((table[i] & (V3D_PTE_VALID | V3D_PTE_WRITE)) ==
			    (V3D_PTE_VALID | V3D_PTE_WRITE));
	}
	bcmv3d_ptes(&job, false);
	CHECK(pt_syncs == 2 && memory[0] == guard && memory[entries + 1] == guard);
	for (i = 0; i < entries; i++)
		CHECK(table[i] == (i < first || i > last ? guard : 0));
	bo->gpu = saved;
	free(memory);
}

static void
check_one(uint32_t request)
{
	struct bcmv3d_device c = { .bo_total = BCMV3D_BO_TOTAL_MAX };
	struct drm_device d = { .dev_private = &c };
	struct drm_file f = { 0 };
	struct drm_v3d_create_bo args = { .size = request, .handle = 99, .offset = 99 };
	struct bcmv3d_bo_stats stats;
	struct bcmv3d_bo *bo;
	struct drm_v3d_mmap_bo mmap_args;
	unsigned int before;

	c.drm = &d;
	fragment_pages = 1;
	CHECK(bcmv3d_create_bo(&d, &args, &f) == 0);
	bo = bcmv3d_to_bo(f.handles[args.handle]);
	CHECK(bo->cma.base.size == rounded(request));
	CHECK(bo->cma.dmasize == bo->cma.base.size && c.allocated == bo->cma.dmasize);
	CHECK(bo->maxsegs == (int)(bo->cma.dmasize / PAGE_SIZE));
	CHECK(bo->nsegs == bo->maxsegs && bo->cma.dmamap->dm_nsegs == bo->maxsegs);
	CHECK(bo->gpu >= BCMV3D_VA_START && bo->gpu + bo->cma.dmasize <=
	    BCMV3D_VA_START + BCMV3D_VA_SIZE);
	CHECK(((unsigned char *)bo->cma.vaddr)[0] == 0);
	CHECK(((unsigned char *)bo->cma.vaddr)[bo->cma.dmasize - 1] == 0);
	bcmv3d_bo_snapshot(&c, &stats);
	CHECK(stats.max_bo_bytes == UINT64_C(256) * 1024 * 1024);
	CHECK(stats.reserved_bytes == rounded(request) && stats.reservations == 1);
	CHECK(stats.releases == 0 && stats.failures == 0);
	mmap_args = (struct drm_v3d_mmap_bo) { .handle = args.handle };
	CHECK(bcmv3d_mmap_bo(&d, &mmap_args, &f) == 0 && mmap_args.offset != 0);
	check_mmap(&bo->cma.base, bo);
	if (PAGE_SIZE == 4096)
		check_ptes(bo);
	if (request == BCMV3D_BO_MAX) {
		struct drm_v3d_create_bo full = { .size = PAGE_SIZE };
		before = kmem_calls;
		CHECK(bcmv3d_create_bo(&d, &full, &f) == -ENOMEM);
		CHECK(full.handle == 0 && full.offset == 0 && kmem_calls == before + 1);
		bcmv3d_bo_snapshot(&c, &stats);
		CHECK(stats.last_stage == BCMV3D_BO_STAGE_QUOTA && stats.last_errno == ENOMEM);
		CHECK(stats.last_reserved_bytes == BCMV3D_BO_MAX);
		CHECK(stats.reservations == 1 && stats.releases == 0);
	}
	close_handle(&f, args.handle);
	empty(&c);
	bcmv3d_bo_snapshot(&c, &stats);
	CHECK(stats.reserved_bytes == 0 && stats.reservations == stats.releases);
	fragment_pages = 0;
}

static void
check_failures(void)
{
	struct bcmv3d_device c = { .bo_total = BCMV3D_BO_TOTAL_MIN };
	struct drm_device d = { .dev_private = &c };
	struct drm_file f = { 0 };
	struct drm_v3d_create_bo args;
	unsigned int before;
	const uint32_t invalid[] = { 0, BCMV3D_BO_MAX + 1, UINT32_MAX };

	c.drm = &d;
	for (unsigned int i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
		before = kmem_calls;
		args = (struct drm_v3d_create_bo) { .size = invalid[i] };
		CHECK(bcmv3d_create_bo(&d, &args, &f) == -EINVAL);
		CHECK(c.bo_stats.last_stage == BCMV3D_BO_STAGE_REQUEST);
		CHECK(c.bo_stats.last_requested_bytes == invalid[i]);
		CHECK(c.bo_stats.last_rounded_bytes == 0 && kmem_calls == before);
		empty(&c);
	}
	before = kmem_calls;
	args = (struct drm_v3d_create_bo) { .size = BCMV3D_BO_TOTAL_MIN + 1 };
	CHECK(bcmv3d_create_bo(&d, &args, &f) == -ENOMEM);
	CHECK(c.bo_stats.last_stage == BCMV3D_BO_STAGE_QUOTA);
	CHECK(kmem_calls == before + 1 && c.bo_stats.reservations == 0);
	empty(&c);
	for (fail = 1; fail <= 6; fail++) {
		before = kmem_calls;
		args = (struct drm_v3d_create_bo) { .size = 16 * 1024 * 1024 + 4096 };
		CHECK(bcmv3d_create_bo(&d, &args, &f) == (fail == 5 ? -EIO : -ENOMEM));
		CHECK(kmem_calls == before + (fail == 1 ? 1 : 2));
		CHECK(args.handle == 0 && args.offset == 0);
		CHECK(c.bo_stats.reservations == c.bo_stats.releases);
		empty(&c);
	}
	fail = 0;
}

int
main(void)
{
	const uint32_t requests[] = { 16 * 1024 * 1024 + 4096,
	    16 * 1024 * 1024 + 1, 32 * 1024 * 1024, 64 * 1024 * 1024,
	    256 * 1024 * 1024 };

	CHECK(PAGE_SIZE == 4096 || PAGE_SIZE == 65536);
	for (unsigned int i = 0; i < sizeof(requests) / sizeof(requests[0]); i++)
		check_one(requests[i]);
	check_failures();
	printf("PASS actual BO size page%u: %u checks; quota-before-metadata, "
	    "16MiB+padding..256MiB, full mmap and %s\n", (unsigned int)PAGE_SIZE,
	    checks, PAGE_SIZE == 4096 ? "4KiB engine PTE bounds" : "allocator-only 64KiB pages");
	return 0;
}
