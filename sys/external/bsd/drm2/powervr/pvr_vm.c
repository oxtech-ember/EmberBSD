/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Native ownership for Rogue translation tables and deferred retirement.
 * Hardware entries use the existing MIT-licensed A733 MMU encoders.
 * Their Linux PowerVR source pin is recorded in sun60i_a733_gpu_mmu.h.
 */
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/systm.h>

#include <arm/sunxi/sun60i_a733_gpu_mmu.h>
#include "pvr_vm.h"

#define PVR_VM_PAGE	A733_GPU_MMU_PAGE_SIZE
#define PVR_VM_VA_END	(UINT64_C(1) << A733_GPU_MMU_VA_BITS)
#define PVR_VM_PA_END	(UINT64_C(1) << A733_GPU_MMU_PA_BITS)

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
	struct pvr_vm_pt *tables[A733_GPU_MMU_PD_ENTRIES];
	unsigned int used;
};

struct pvr_vm_range {
	struct pvr_vm_range *next;
	uint64_t va, size;
};

struct pvr_vm {
	bus_dma_tag_t dmat;
	struct pvr_vm_dma catalogue;
	struct pvr_vm_pd *dirs[A733_GPU_MMU_PC_ENTRIES];
	struct pvr_vm_range *ranges;
	struct pvr_vm_pd *retired_dirs;
	struct pvr_vm_pt *retired_tables;
	int (*invalidate)(void *);
	void *invalidate_cookie;
	bool failed;
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

int
pvr_vm_create(bus_dma_tag_t dmat, struct pvr_vm **out)
{
	struct pvr_vm *vm;
	int error;

	if (dmat == NULL || out == NULL || *out != NULL)
		return EINVAL;
	vm = kmem_zalloc(sizeof(*vm), KM_SLEEP);
	vm->dmat = dmat;
	error = pvr_vm_dma_alloc(vm, &vm->catalogue);
	if (error != 0) {
		kmem_free(vm, sizeof(*vm));
		return error;
	}
	*out = vm;
	return 0;
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
	pvr_vm_collect(vm);
	return 0;
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
	struct sun60i_a733_gpu_mmu_indices idx;
	uint64_t at, step;
	int error;

	for (at = va; at < va + size; at += step) {
		error = sun60i_a733_gpu_mmu_indices(at, &idx);
		KASSERT(error == 0);
		step = MIN(va + size - at,
		    (uint64_t)(A733_GPU_MMU_PT_ENTRIES - idx.pt) * PVR_VM_PAGE);
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

int
pvr_vm_map(struct pvr_vm *vm, uint64_t va, uint64_t pa, uint64_t size,
    uint64_t flags)
{
	struct pvr_vm_range **slot, *range;
	struct pvr_vm_pd *pd;
	struct pvr_vm_pt *pt;
	struct sun60i_a733_gpu_mmu_indices idx;
	uint64_t at, step, value, *entries;
	uint32_t pce;
	unsigned int n, count, pc;
	int error;

	if (vm != NULL && vm->failed)
		return EIO;
	if (vm == NULL || !pvr_vm_range_valid(va, size) ||
	    pa >= PVR_VM_PA_END || size > PVR_VM_PA_END - pa ||
	    sun60i_a733_gpu_mmu_pte(pa, flags, &value) != 0)
		return EINVAL;
	for (slot = &vm->ranges; *slot != NULL; slot = &(*slot)->next) {
		if (va < (*slot)->va + (*slot)->size && (*slot)->va < va + size)
			return EEXIST;
		if ((*slot)->va >= va)
			break;
	}
	range = kmem_zalloc(sizeof(*range), KM_SLEEP);
	range->va = va;
	range->size = size;
	/* Reserve every missing table before modifying any existing DMA bytes. */
	for (at = va; at < va + size; at += step) {
		error = sun60i_a733_gpu_mmu_indices(at, &idx);
		KASSERT(error == 0);
		step = MIN(va + size - at,
		    (uint64_t)(A733_GPU_MMU_PT_ENTRIES - idx.pt) * PVR_VM_PAGE);
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
		error = sun60i_a733_gpu_mmu_indices(at, &idx);
		KASSERT(error == 0);
		step = MIN(va + size - at,
		    (uint64_t)(A733_GPU_MMU_PT_ENTRIES - idx.pt) * PVR_VM_PAGE);
		count = step / PVR_VM_PAGE;
		pd = vm->dirs[idx.pc];
		pt = pd->tables[idx.pd];
		entries = pt->dma.cpu;
		for (n = 0; n < count; n++) {
			KASSERT(entries[idx.pt + n] == 0);
			error = sun60i_a733_gpu_mmu_pte(pa + at - va +
			    (uint64_t)n * PVR_VM_PAGE, flags, &value);
			KASSERT(error == 0);
			entries[idx.pt + n] = htole64(value);
		}
		pt->used += count;
		pvr_vm_dma_sync(vm, &pt->dma);
		error = sun60i_a733_gpu_mmu_pde(pt->dma.pa, &value);
		KASSERT(error == 0);
		((uint64_t *)pd->dma.cpu)[idx.pd] = htole64(value);
	}
	for (pc = va >> 30; pc <= (va + size - 1) >> 30; pc++) {
		pd = vm->dirs[pc];
		pvr_vm_dma_sync(vm, &pd->dma);
		error = sun60i_a733_gpu_mmu_pce(pd->dma.pa, &pce);
		KASSERT(error == 0);
		((uint32_t *)vm->catalogue.cpu)[pc] = htole32(pce);
	}
	pvr_vm_dma_sync(vm, &vm->catalogue);
	range->next = *slot;
	*slot = range;
	return 0;
fail:
	pvr_vm_rollback(vm, va, size);
	kmem_free(range, sizeof(*range));
	return error;
}

int
pvr_vm_unmap(struct pvr_vm *vm, uint64_t va, uint64_t size)
{
	struct pvr_vm_range **slot, *range;
	struct pvr_vm_pd *pd;
	struct pvr_vm_pt *pt;
	struct sun60i_a733_gpu_mmu_indices idx;
	uint64_t at, step, *entries;
	unsigned int n, count;
	int error;

	if (vm != NULL && vm->failed)
		return EIO;
	if (vm == NULL || !pvr_vm_range_valid(va, size))
		return EINVAL;
	for (slot = &vm->ranges; *slot != NULL; slot = &(*slot)->next)
		if ((*slot)->va == va && (*slot)->size == size)
			break;
	if (*slot == NULL)
		return ENOENT;
	range = *slot;
	for (at = va; at < va + size; at += step) {
		error = sun60i_a733_gpu_mmu_indices(at, &idx);
		KASSERT(error == 0);
		step = MIN(va + size - at,
		    (uint64_t)(A733_GPU_MMU_PT_ENTRIES - idx.pt) * PVR_VM_PAGE);
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
	error = pvr_vm_flush(vm);
	if (error != 0)
		return error;
	*slot = range->next;
	kmem_free(range, sizeof(*range));
	return 0;
}

void
pvr_vm_destroy(struct pvr_vm *vm)
{
	int error;

	if (vm == NULL)
		return;
	KASSERT(vm->invalidate == NULL && !vm->failed);
	while (vm->ranges != NULL) {
		error = pvr_vm_unmap(vm, vm->ranges->va, vm->ranges->size);
		KASSERT(error == 0);
	}
	pvr_vm_dma_free(vm, &vm->catalogue);
	kmem_free(vm, sizeof(*vm));
}
