/* Origin: EmberBSD native GPU PTE granule and role contract, 2026-10-21. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned checks;
#define CHECK(e) do { checks++; if (!(e)) { \
	fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #e); exit(1); \
} } while (0)
#ifndef __arraycount
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#endif
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define KASSERT(e) CHECK(e)
#define BUS_DMASYNC_PREWRITE 1
typedef int kmutex_t;
typedef int spinlock_t;
typedef struct { uint64_t ds_addr; size_t ds_len; } bus_dma_segment_t;
struct dma_map { int dm_nsegs; bus_dma_segment_t *dm_segs; };
struct bcmv3d_bo;
struct drm_gem_object { struct bcmv3d_bo *bo; };
struct bcmv3d_bo {
	struct { struct drm_gem_object base; void *vaddr;
	    struct dma_map *dmamap; size_t dmasize; } cma;
	uint32_t gpu;
};
#include "ptes-types.h"
static struct bcmv3d_bo *bcmv3d_to_bo(struct drm_gem_object *obj)
{ CHECK(obj != NULL && obj->bo != NULL); return obj->bo; }
static unsigned syncs;
static void bcmv3d_sync(struct bcmv3d_bo *bo, int flags)
{ CHECK(bo->cma.vaddr != NULL && flags == BUS_DMASYNC_PREWRITE); syncs++; }
#include "ptes-body.h"

static void
exercise(bool tfu, bool alias)
{
	const uint32_t guard = 0xa55ac33c;
	const unsigned entries = (4 * 1024 * 1024) / sizeof(uint32_t);
	struct bcmv3d_bo bo[4] = {0}, pt = {0};
	struct drm_gem_object *objects[4];
	struct dma_map maps[4]; bus_dma_segment_t segments[4][2];
	struct bcmv3d_job job = {0};
	uint32_t *memory = malloc((entries + 2) * sizeof(*memory));
	CHECK(memory != NULL && (PAGE_SIZE == 4096 || PAGE_SIZE == 65536));
	uint32_t *table = memory + 1; pt.cma.vaddr = table; job.pt = &pt;
	for (unsigned i = 0; i < __arraycount(bo); i++) {
		bo[i].gpu = 0x1000000 + i * 0x100000;
		bo[i].cma.base.bo = &bo[i]; objects[i] = &bo[i].cma.base;
		bo[i].cma.dmasize = 3 * PAGE_SIZE;
		segments[i][0] = (bus_dma_segment_t){0x50000000 + i * 0x1000000, PAGE_SIZE};
		segments[i][1] = (bus_dma_segment_t){0x60000000 + i * 0x1000000, 2 * PAGE_SIZE};
		maps[i] = (struct dma_map){2, segments[i]}; bo[i].cma.dmamap = &maps[i];
	}
	job.bos = objects; job.nbo = __arraycount(bo);
#ifdef PTE_TFU_VECTOR
	if (tfu) {
		job.nbo = 0; job.bos = NULL; job.ntfu = alias ? 1 : 4;
		job.src = &bo[0]; job.dst = alias ? &bo[0] : &bo[1];
		/* Deliberately different from source/destination order. */
		for (unsigned i = 0; i < job.ntfu; i++)
			job.tfu_bos[i] = &bo[job.ntfu - i - 1];
	}
#else
	CHECK(!tfu && !alias);
#endif
	for (unsigned i = 0; i < entries + 2; i++) memory[i] = guard;
	unsigned before = syncs;
	bcmv3d_ptes(&job, true); CHECK(syncs == before + 1);
	for (unsigned b = 0; b < __arraycount(bo); b++) {
		unsigned count = (unsigned)(bo[b].cma.dmasize / 4096);
		for (unsigned p = 0; p < count; p++) {
			uint32_t expected = guard;
			if (!alias || b == 0) {
				uint64_t address = p < PAGE_SIZE / 4096 ?
				    segments[b][0].ds_addr + (uint64_t)p * 4096 :
				    segments[b][1].ds_addr + (uint64_t)(p - PAGE_SIZE / 4096) * 4096;
				expected = (uint32_t)(address >> 12) | V3D_PTE_VALID;
				if (!tfu || b == (alias ? 0U : 1U)) expected |= V3D_PTE_WRITE;
			}
			CHECK(table[(bo[b].gpu >> 12) + p] == expected);
		}
	}
	/* Every unrelated PTE, including adjacent and private slots, is untouched. */
	for (unsigned p = 0; p < entries; p++) {
		bool selected = false;
		for (unsigned b = 0; b < (alias ? 1U : 4U); b++) {
			unsigned first = bo[b].gpu >> 12;
			selected |= p >= first && p - first < bo[b].cma.dmasize / 4096;
		}
		if (!selected) CHECK(table[p] == guard);
	}
	bcmv3d_ptes(&job, false); CHECK(syncs == before + 2);
	for (unsigned p = 0; p < entries; p++) {
		bool selected = false;
		for (unsigned b = 0; b < (alias ? 1U : 4U); b++) {
			unsigned first = bo[b].gpu >> 12;
			selected |= p >= first && p - first < bo[b].cma.dmasize / 4096;
		}
		CHECK(table[p] == (selected ? 0U : guard));
	}
	CHECK(memory[0] == guard && memory[entries + 1] == guard);
	free(memory);
}
int
main(void)
{
	exercise(false, false);
#ifdef PTE_TFU_VECTOR
	exercise(true, false); exercise(true, true);
#endif
	printf("PASS: %u actual PTE checks with host PAGE_SIZE=%u\n", checks, (unsigned)PAGE_SIZE);
	return 0;
}
