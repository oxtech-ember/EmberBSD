/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Native ownership for Rogue translation tables and deferred retirement.
 * Hardware entries use the common MIT-licensed PowerVR MMU encoders.
 * Their Linux PowerVR source pin is recorded in pvr_mmu.h.
 */
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/rbtree.h>
#include <sys/systm.h>

#include <dev/powervr/pvr_mmu.h>
#include "pvr_vm.h"

#define PVR_VM_PAGE	PVR_MMU_PAGE_SIZE
#define PVR_VM_VA_END	(UINT64_C(1) << PVR_MMU_VA_BITS)
#define PVR_VM_PA_END	(UINT64_C(1) << PVR_MMU_PA_BITS)

struct pvr_vm_dma {
	bus_dma_segment_t segment;
	bus_dmamap_t map;
	void *cpu;
	uint64_t pa;
	int nsegments;
	bool loaded, synced;
};

struct pvr_vm_pt {
	struct pvr_vm_pt *retired_next;
	struct pvr_vm_dma dma;
	unsigned int used;
};

struct pvr_vm_pd {
	struct pvr_vm_pd *retired_next;
	struct pvr_vm_dma dma;
	struct pvr_vm_pt *tables[PVR_MMU_PD_ENTRIES];
	unsigned int used;
};

/* Mapped ranges are disjoint; the tree orders them by start address. */
struct pvr_vm_range {
	struct rb_node node;
	uint64_t va, size;
};

struct pvr_vm {
	bus_dma_tag_t dmat;
	struct pvr_vm_dma catalogue;
	struct pvr_vm_pd *dirs[PVR_MMU_PC_ENTRIES];
	struct rb_tree ranges;
	struct pvr_vm_pd *retired_dirs;
	struct pvr_vm_pt *retired_tables;
	int (*invalidate)(void *);
	void *invalidate_cookie;
	/* dirty: an entry changed after the last device invalidation. */
	bool failed, direct, dirty;
};

static void
pvr_vm_dma_free(struct pvr_vm *vm, struct pvr_vm_dma *dma)
{

	if (dma->loaded && dma->synced)
		bus_dmamap_sync(vm->dmat, dma->map, 0, PVR_VM_PAGE,
		    BUS_DMASYNC_POSTWRITE);
	if (dma->loaded)
		bus_dmamap_unload(vm->dmat, dma->map);
	if (dma->map != NULL)
		bus_dmamap_destroy(vm->dmat, dma->map);
	if (dma->cpu != NULL)
		bus_dmamem_unmap(vm->dmat, dma->cpu, PVR_VM_PAGE);
	if (dma->nsegments != 0)
		bus_dmamem_free(vm->dmat, &dma->segment, dma->nsegments);
	memset(dma, 0, sizeof(*dma));
}

static void
pvr_vm_dma_sync(struct pvr_vm *vm, struct pvr_vm_dma *dma)
{

	bus_dmamap_sync(vm->dmat, dma->map, 0, PVR_VM_PAGE,
	    BUS_DMASYNC_PREWRITE);
	dma->synced = true;
}

static int
pvr_vm_dma_alloc(struct pvr_vm *vm, struct pvr_vm_dma *dma)
{
	int error;

	error = bus_dmamem_alloc(vm->dmat, PVR_VM_PAGE, PVR_VM_PAGE, 0,
	    &dma->segment, 1, &dma->nsegments, BUS_DMA_WAITOK);
	if (error != 0) {
		dma->nsegments = 0;
		return error;
	}
	if (dma->nsegments != 1) {
		error = EINVAL;
		goto fail;
	}
	error = bus_dmamem_map(vm->dmat, &dma->segment, 1, PVR_VM_PAGE,
	    &dma->cpu, BUS_DMA_WAITOK | BUS_DMA_COHERENT);
	if (error != 0) {
		dma->cpu = NULL;
		goto fail;
	}
	error = bus_dmamap_create(vm->dmat, PVR_VM_PAGE, 1, PVR_VM_PAGE, 0,
	    BUS_DMA_WAITOK, &dma->map);
	if (error != 0) {
		dma->map = NULL;
		goto fail;
	}
	error = bus_dmamap_load(vm->dmat, dma->map, dma->cpu, PVR_VM_PAGE,
	    NULL, BUS_DMA_WAITOK);
	if (error != 0)
		goto fail;
	dma->loaded = true;
	if ((dma->map->_dm_flags & (_BUS_DMAMAP_COHERENT |
	    _BUS_DMAMAP_IS_BOUNCING)) != _BUS_DMAMAP_COHERENT) {
		error = EOPNOTSUPP;
		goto fail;
	}
	if (dma->map->dm_nsegs != 1 || dma->map->dm_mapsize != PVR_VM_PAGE ||
	    dma->map->dm_segs[0].ds_len != PVR_VM_PAGE) {
		error = EINVAL;
		goto fail;
	}
	dma->pa = dma->map->dm_segs[0].ds_addr;
	if (vm->direct && dma->pa != dma->segment.ds_addr) {
		error = EOPNOTSUPP;
		goto fail;
	}
	if ((dma->pa & (PVR_VM_PAGE - 1)) != 0 ||
	    dma->pa > PVR_VM_PA_END - PVR_VM_PAGE) {
		error = EINVAL;
		goto fail;
	}
	memset(dma->cpu, 0, PVR_VM_PAGE);
	pvr_vm_dma_sync(vm, dma);
	return 0;
fail:
	pvr_vm_dma_free(vm, dma);
	return error;
}

static bool
pvr_vm_range_valid(uint64_t va, uint64_t size)
{

	return size != 0 && ((va | size) & (PVR_VM_PAGE - 1)) == 0 &&
	    va < PVR_VM_VA_END && size <= PVR_VM_VA_END - va;
}

static int
pvr_vm_range_compare_nodes(void *context, const void *a, const void *b)
{
	const struct pvr_vm_range *left = a, *right = b;

	if (left->va < right->va)
		return -1;
	return left->va > right->va;
}

static int
pvr_vm_range_compare_key(void *context, const void *node, const void *key)
{
	const struct pvr_vm_range *range = node;
	const uint64_t va = *(const uint64_t *)key;

	if (range->va < va)
		return -1;
	return range->va > va;
}

static const rb_tree_ops_t pvr_vm_range_ops = {
	.rbto_compare_nodes = pvr_vm_range_compare_nodes,
	.rbto_compare_key = pvr_vm_range_compare_key,
	.rbto_node_offset = offsetof(struct pvr_vm_range, node),
	.rbto_context = NULL,
};

static int
pvr_vm_create_internal(bus_dma_tag_t dmat, bool direct, struct pvr_vm **out)
{
	struct pvr_vm *vm;
	int error;

	if (dmat == NULL || out == NULL || *out != NULL)
		return EINVAL;
	vm = kmem_zalloc(sizeof(*vm), KM_SLEEP);
	vm->dmat = dmat;
	vm->direct = direct;
	rb_tree_init(&vm->ranges, &pvr_vm_range_ops);
	error = pvr_vm_dma_alloc(vm, &vm->catalogue);
	if (error != 0) {
		kmem_free(vm, sizeof(*vm));
		return error;
	}
	*out = vm;
	return 0;
}

int
pvr_vm_create(bus_dma_tag_t dmat, struct pvr_vm **out)
{
	return pvr_vm_create_internal(dmat, false, out);
}

int
pvr_vm_create_direct(bus_dma_tag_t dmat, struct pvr_vm **out)
{
	return pvr_vm_create_internal(dmat, true, out);
}

/* CPU view of the current leaf, not a sample of a GPU TLB. */
bool
pvr_vm_cpu_leaf(const struct pvr_vm *vm, uint64_t va, uint64_t *pte)
{
	const struct pvr_vm_pd *pd;
	const struct pvr_vm_pt *pt;
	struct pvr_mmu_indices idx;

	if (pvr_mmu_indices(va, &idx) != 0)
		return false;
	pd = vm->dirs[idx.pc];
	pt = pd != NULL ? pd->tables[idx.pd] : NULL;
	*pte = pt == NULL ? 0 : le64toh(((const uint64_t *)pt->dma.cpu)[idx.pt]);
	return true;
}

uint64_t
pvr_vm_root_pa(const struct pvr_vm *vm)
{

	KASSERT(vm != NULL);
	return vm->catalogue.pa;
}

/* Cache invalidation must finish before a detached table becomes reusable. */
static void
pvr_vm_collect(struct pvr_vm *vm)
{
	struct pvr_vm_pd *pd;
	struct pvr_vm_pt *pt;

	while ((pt = vm->retired_tables) != NULL) {
		vm->retired_tables = pt->retired_next;
		pvr_vm_dma_free(vm, &pt->dma);
		kmem_free(pt, sizeof(*pt));
	}
	while ((pd = vm->retired_dirs) != NULL) {
		vm->retired_dirs = pd->retired_next;
		pvr_vm_dma_free(vm, &pd->dma);
		kmem_free(pd, sizeof(*pd));
	}
}

int
pvr_vm_flush(struct pvr_vm *vm)
{
	int error;

	if (vm == NULL)
		return EINVAL;
	if (vm->failed)
		return EIO;
	if (vm->invalidate != NULL) {
		error = vm->invalidate(vm->invalidate_cookie);
		if (error != 0) {
			vm->failed = true;
			return error;
		}
	}
	vm->dirty = false;
	pvr_vm_collect(vm);
	return 0;
}

/* A device invalidation costs a firmware round trip; skip it when clean. */
int
pvr_vm_flush_changes(struct pvr_vm *vm)
{
	int error;

	if (vm == NULL)
		return EINVAL;
	if (vm->failed)
		return EIO;
	if (!vm->dirty)
		return 0;
	if (vm->invalidate != NULL) {
		error = vm->invalidate(vm->invalidate_cookie);
		if (error != 0) {
			vm->failed = true;
			return error;
		}
	}
	vm->dirty = false;
	return 0;
}

bool
pvr_vm_reclaim(struct pvr_vm *vm)
{

	if (vm == NULL || vm->failed)
		return false;
	pvr_vm_collect(vm);
	return true;
}

int
pvr_vm_publish(struct pvr_vm *vm, int (*invalidate)(void *), void *cookie)
{

	if (vm == NULL || invalidate == NULL)
		return EINVAL;
	if (vm->invalidate != NULL || vm->failed)
		return EBUSY;
	vm->invalidate = invalidate;
	vm->invalidate_cookie = cookie;
	return pvr_vm_flush(vm);
}

/* The owner first drains every job and retires all firmware contexts. */
int
pvr_vm_quiesce(struct pvr_vm *vm)
{
	int error;

	error = pvr_vm_flush(vm);
	if (error != 0)
		return error;
	vm->invalidate = NULL;
	vm->invalidate_cookie = NULL;
	return 0;
}

/* Free only provisional empty tables. No hardware entry was written yet. */
static void
pvr_vm_rollback(struct pvr_vm *vm, uint64_t va, uint64_t size)
{
	struct pvr_vm_pd *pd;
	struct pvr_vm_pt *pt;
	struct pvr_mmu_indices idx;
	uint64_t at, step;
	int error;

	for (at = va; at < va + size; at += step) {
		error = pvr_mmu_indices(at, &idx);
		KASSERT(error == 0);
		step = MIN(va + size - at,
		    (uint64_t)(PVR_MMU_PT_ENTRIES - idx.pt) * PVR_VM_PAGE);
		pd = vm->dirs[idx.pc];
		if (pd == NULL)
			continue;
		pt = pd->tables[idx.pd];
		if (pt != NULL && pt->used == 0) {
			pvr_vm_dma_free(vm, &pt->dma);
			kmem_free(pt, sizeof(*pt));
			pd->tables[idx.pd] = NULL;
			pd->used--;
		}
		if (pd->used == 0) {
			pvr_vm_dma_free(vm, &pd->dma);
			kmem_free(pd, sizeof(*pd));
			vm->dirs[idx.pc] = NULL;
		}
	}
}

static int
pvr_vm_map_internal(struct pvr_vm *vm, uint64_t va, uint64_t pa,
    const uint64_t *pages, uint64_t size, uint64_t flags)
{
	struct pvr_vm_range *range, *inserted __diagused;
	struct pvr_vm_pd *pd;
	struct pvr_vm_pt *pt;
	struct pvr_mmu_indices idx;
	uint64_t at, last, step, value, *entries;
	uint32_t pce;
	unsigned int n, count, pc;
	int error;

	if (vm != NULL && vm->failed)
		return EIO;
	if (vm == NULL || !pvr_vm_range_valid(va, size))
		return EINVAL;
	/* Validate every address before allocating tables or publishing any PTE. */
	if (pages == NULL) {
		if (pa >= PVR_VM_PA_END || size > PVR_VM_PA_END - pa ||
		    pvr_mmu_pte(pa, flags, &value) != 0)
			return EINVAL;
	} else {
		for (at = 0; at < size / PVR_VM_PAGE; at++)
			if (pvr_mmu_pte(pages[at], flags, &value) != 0)
				return EINVAL;
	}
	/* Only the last range starting at or before our end can overlap us. */
	last = va + size - 1;
	range = rb_tree_find_node_leq(&vm->ranges, &last);
	if (range != NULL && range->va + range->size > va)
		return EEXIST;
	range = kmem_zalloc(sizeof(*range), KM_SLEEP);
	range->va = va;
	range->size = size;
	/* Reserve every missing table before modifying any existing DMA bytes. */
	for (at = va; at < va + size; at += step) {
		error = pvr_mmu_indices(at, &idx);
		KASSERT(error == 0);
		step = MIN(va + size - at,
		    (uint64_t)(PVR_MMU_PT_ENTRIES - idx.pt) * PVR_VM_PAGE);
		pd = vm->dirs[idx.pc];
		if (pd == NULL) {
			pd = kmem_zalloc(sizeof(*pd), KM_SLEEP);
			error = pvr_vm_dma_alloc(vm, &pd->dma);
			if (error != 0) {
				kmem_free(pd, sizeof(*pd));
				goto fail;
			}
			vm->dirs[idx.pc] = pd;
		}
		if (pd->tables[idx.pd] == NULL) {
			pt = kmem_zalloc(sizeof(*pt), KM_SLEEP);
			error = pvr_vm_dma_alloc(vm, &pt->dma);
			if (error != 0) {
				kmem_free(pt, sizeof(*pt));
				goto fail;
			}
			pd->tables[idx.pd] = pt;
			pd->used++;
		}
	}
	/* Every encoder input is validated. This phase has no fallible operation. */
	for (at = va; at < va + size; at += step) {
		error = pvr_mmu_indices(at, &idx);
		KASSERT(error == 0);
		step = MIN(va + size - at,
		    (uint64_t)(PVR_MMU_PT_ENTRIES - idx.pt) * PVR_VM_PAGE);
		count = step / PVR_VM_PAGE;
		pd = vm->dirs[idx.pc];
		pt = pd->tables[idx.pd];
		entries = pt->dma.cpu;
		for (n = 0; n < count; n++) {
			KASSERT(entries[idx.pt + n] == 0);
			error = pvr_mmu_pte(pages != NULL ?
			    pages[(at - va) / PVR_VM_PAGE + n] :
			    pa + at - va + (uint64_t)n * PVR_VM_PAGE, flags, &value);
			KASSERT(error == 0);
			entries[idx.pt + n] = htole64(value);
		}
		pt->used += count;
		pvr_vm_dma_sync(vm, &pt->dma);
		error = pvr_mmu_pde(pt->dma.pa, &value);
		KASSERT(error == 0);
		((uint64_t *)pd->dma.cpu)[idx.pd] = htole64(value);
	}
	for (pc = va >> 30; pc <= (va + size - 1) >> 30; pc++) {
		pd = vm->dirs[pc];
		pvr_vm_dma_sync(vm, &pd->dma);
		error = pvr_mmu_pce(pd->dma.pa, &pce);
		KASSERT(error == 0);
		((uint32_t *)vm->catalogue.cpu)[pc] = htole32(pce);
	}
	pvr_vm_dma_sync(vm, &vm->catalogue);
	vm->dirty = true;
	inserted = rb_tree_insert_node(&vm->ranges, range);
	KASSERT(inserted == range);
	return 0;
fail:
	pvr_vm_rollback(vm, va, size);
	kmem_free(range, sizeof(*range));
	return error;
}

int
pvr_vm_map(struct pvr_vm *vm, uint64_t va, uint64_t pa, uint64_t size,
    uint64_t flags)
{

	return pvr_vm_map_internal(vm, va, pa, NULL, size, flags);
}

int
pvr_vm_map_pages(struct pvr_vm *vm, uint64_t va, const uint64_t *pages,
    uint64_t size, uint64_t flags)
{

	if (pages == NULL)
		return EINVAL;
	return pvr_vm_map_internal(vm, va, 0, pages, size, flags);
}

static int
pvr_vm_unmap_internal(struct pvr_vm *vm, uint64_t va, uint64_t size,
    bool flush)
{
	struct pvr_vm_range *range;
	struct pvr_vm_pd *pd;
	struct pvr_vm_pt *pt;
	struct pvr_mmu_indices idx;
	uint64_t at, step, *entries;
	unsigned int n, count;
	int error;

	if (vm != NULL && vm->failed)
		return EIO;
	if (vm == NULL || !pvr_vm_range_valid(va, size))
		return EINVAL;
	range = rb_tree_find_node(&vm->ranges, &va);
	if (range == NULL || range->size != size)
		return ENOENT;
	for (at = va; at < va + size; at += step) {
		error = pvr_mmu_indices(at, &idx);
		KASSERT(error == 0);
		step = MIN(va + size - at,
		    (uint64_t)(PVR_MMU_PT_ENTRIES - idx.pt) * PVR_VM_PAGE);
		count = step / PVR_VM_PAGE;
		pd = vm->dirs[idx.pc];
		pt = pd->tables[idx.pd];
		entries = pt->dma.cpu;
		for (n = 0; n < count; n++) {
			KASSERT(entries[idx.pt + n] != 0);
			entries[idx.pt + n] = 0;
		}
		pt->used -= count;
		pvr_vm_dma_sync(vm, &pt->dma);
		if (pt->used != 0)
			continue;
		((uint64_t *)pd->dma.cpu)[idx.pd] = 0;
		pvr_vm_dma_sync(vm, &pd->dma);
		pd->tables[idx.pd] = NULL;
		pd->used--;
		pt->retired_next = vm->retired_tables;
		vm->retired_tables = pt;
		if (pd->used != 0)
			continue;
		((uint32_t *)vm->catalogue.cpu)[idx.pc] = 0;
		pvr_vm_dma_sync(vm, &vm->catalogue);
		vm->dirs[idx.pc] = NULL;
		pd->retired_next = vm->retired_dirs;
		vm->retired_dirs = pd;
	}
	vm->dirty = true;
	if (flush) {
		error = pvr_vm_flush(vm);
		if (error != 0)
			return error;
	}
	rb_tree_remove_node(&vm->ranges, range);
	kmem_free(range, sizeof(*range));
	return 0;
}

int
pvr_vm_unmap(struct pvr_vm *vm, uint64_t va, uint64_t size)
{

	return pvr_vm_unmap_internal(vm, va, size, true);
}

int
pvr_vm_unmap_deferred(struct pvr_vm *vm, uint64_t va, uint64_t size)
{

	return pvr_vm_unmap_internal(vm, va, size, false);
}

void
pvr_vm_destroy(struct pvr_vm *vm)
{
	struct pvr_vm_range *range;
	int error;

	if (vm == NULL)
		return;
	KASSERT(vm->invalidate == NULL && !vm->failed);
	while ((range = RB_TREE_MIN(&vm->ranges)) != NULL) {
		error = pvr_vm_unmap(vm, range->va, range->size);
		KASSERT(error == 0);
	}
	/* Deferred unmaps may have left detached tables. */
	pvr_vm_collect(vm);
	pvr_vm_dma_free(vm, &vm->catalogue);
	kmem_free(vm, sizeof(*vm));
}
