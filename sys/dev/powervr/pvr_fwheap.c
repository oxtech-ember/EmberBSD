/* Origin: EmberBSD; A733-profile firmware heap mechanism. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/systm.h>

#include "pvr_fwheap.h"
#include "pvr_fwcompute_file.h"
#include "pvr_fwcompute_vm.h"
#include "pvr_mips.h"

/* Same discipline as the firmware memory owner: coherent, single segment,
 * page-aligned, inside 36 bits, zeroed by the allocator. */
int
pvr_heap_dma_alloc_coherent(bus_dma_tag_t dmat, size_t size,
    struct pvr_dma *dma)
{
	int error;

	if (size == 0 || size > 256 * 1024 || (size & 4095) != 0)
		return EINVAL;
	dma->size = size;
	error = bus_dmamem_alloc(dmat, size, 4096, 0, &dma->segment, 1,
	    &dma->nsegments, BUS_DMA_WAITOK);
	if (error != 0) {
		dma->nsegments = 0;
		return error;
	}
	if (dma->nsegments != 1) {
		error = EINVAL;
		goto fail;
	}
	error = bus_dmamem_map(dmat, &dma->segment, 1, size, &dma->cpu,
	    BUS_DMA_WAITOK | BUS_DMA_COHERENT);
	if (error != 0) {
		dma->cpu = NULL;
		goto fail;
	}
	error = bus_dmamap_create(dmat, size, 1, size, 0, BUS_DMA_WAITOK,
	    &dma->map);
	if (error != 0) {
		dma->map = NULL;
		goto fail;
	}
	error = bus_dmamap_load(dmat, dma->map, dma->cpu, size, NULL,
	    BUS_DMA_WAITOK);
	if (error != 0)
		goto fail;
	dma->loaded = true;
	if ((dma->map->_dm_flags & (_BUS_DMAMAP_COHERENT |
	    _BUS_DMAMAP_IS_BOUNCING)) != _BUS_DMAMAP_COHERENT) {
		error = EOPNOTSUPP;
		goto fail;
	}
	dma->coherent = true;
	if (dma->map->dm_nsegs != 1 || dma->map->dm_segs[0].ds_len != size) {
		error = EINVAL;
		goto fail;
	}
	dma->address = dma->map->dm_segs[0].ds_addr;
	if ((dma->address & 4095) != 0 ||
	    dma->address > (UINT64_C(1) << 36) - size) {
		error = EINVAL;
		goto fail;
	}
	memset(dma->cpu, 0, size);
	return 0;
fail:
	if (dma->loaded) {
		bus_dmamap_unload(dmat, dma->map);
		dma->loaded = false;
	}
	if (dma->map != NULL) {
		bus_dmamap_destroy(dmat, dma->map);
		dma->map = NULL;
	}
	if (dma->cpu != NULL) {
		bus_dmamem_unmap(dmat, dma->cpu, dma->size);
		dma->cpu = NULL;
	}
	if (dma->nsegments != 0) {
		bus_dmamem_free(dmat, &dma->segment, 1);
		dma->nsegments = 0;
	}
	return error;
}

void
pvr_heap_dma_free(bus_dma_tag_t dmat, struct pvr_dma *dma)
{

	if (dma->loaded)
		bus_dmamap_unload(dmat, dma->map);
	if (dma->map != NULL)
		bus_dmamap_destroy(dmat, dma->map);
	if (dma->cpu != NULL)
		bus_dmamem_unmap(dmat, dma->cpu, dma->size);
	if (dma->nsegments != 0)
		bus_dmamem_free(dmat, &dma->segment, 1);
	memset(dma, 0, sizeof(*dma));
}

static bool
fwheap_dma_valid(const struct pvr_dma *dma)
{

	return dma->loaded && dma->coherent && dma->cpu != NULL &&
	    dma->map != NULL && dma->map->dm_nsegs == 1 &&
	    dma->map->dm_segs[0].ds_addr == dma->address &&
	    (dma->address & 4095) == 0 && dma->address <=
	    (UINT64_C(1) << 36) - dma->size;
}

/* The firmware wrapper table is regular memory owned by fwmem; only its
 * shape matters here, not its cache attributes. */
static bool
fwheap_table_valid(const struct pvr_dma *dma)
{

	return dma->loaded && dma->cpu != NULL && dma->map != NULL &&
	    dma->map->dm_nsegs == 1 &&
	    dma->map->dm_segs[0].ds_addr == dma->address &&
	    (dma->address & 4095) == 0 &&
	    dma->address <= (UINT64_C(1) << 36) - dma->size;
}

static bool
fwheap_dma_overlap(const struct pvr_dma *a,
    const struct pvr_dma *b)
{

	return a->address < b->address + b->size &&
	    b->address < a->address + a->size;
}

int
pvr_fwheap_prepare(bus_dma_tag_t dmat,
    struct pvr_fwmem *mem,
    const struct pvr_fwcompute_image *image,
    struct pvr_fwheap **out)
{
	struct pvr_fwheap *heap;
	uint8_t *bytes;
	int error;

	if (out == NULL || *out != NULL || dmat == NULL || mem == NULL ||
	    image == NULL || image->used_bytes != PVR_FWCOMPUTE_USED_BYTES)
		return EINVAL;
	if (mem->profile != PVR_FW_PROFILE_A733 &&
	    mem->profile != PVR_FW_PROFILE_GE8300)
		return EOPNOTSUPP;
	heap = kmem_zalloc(sizeof(*heap), KM_SLEEP);
	heap->dmat = dmat;
	heap->owner = mem;
	heap->profile = mem->profile;
	if (heap->profile == PVR_FW_PROFILE_GE8300) {
		if (dmat != mem->dmat) {
			error = EINVAL;
			goto fail;
		}
		error = pvr_fwmem_compute_alloc(mem, PVR_FWHEAP_BYTES,
		    &heap->heap);
	} else {
		error = pvr_heap_dma_alloc_coherent(dmat, PVR_FWHEAP_BYTES,
		    &heap->heap);
	}
	if (error != 0)
		goto fail;
	if (!fwheap_dma_valid(&heap->heap) ||
	    fwheap_dma_overlap(&heap->heap, &mem->code) ||
	    fwheap_dma_overlap(&heap->heap, &mem->data) ||
	    fwheap_dma_overlap(&heap->heap, &mem->table) ||
	    fwheap_dma_overlap(&heap->heap, &mem->work) ||
	    fwheap_dma_overlap(&heap->heap, &mem->config)) {
		error = EINVAL;
		goto fail;
	}
	bytes = heap->heap.cpu;
	if (heap->profile == PVR_FW_PROFILE_GE8300)
		memset(bytes, 0, heap->heap.size);
	/* The serializer's object bytes land at their firmware addresses. */
	memcpy(bytes + PVR_FWHEAP_CONTEXT_OFF, &image->context,
	    sizeof(image->context));
	memcpy(bytes + PVR_FWHEAP_CTL_OFF, &image->ctl,
	    sizeof(image->ctl));
	memcpy(bytes + PVR_FWHEAP_CCCB_OFF, image->cccb,
	    PVR_FWCOMPUTE_CCCB_SIZE);
	memcpy(bytes + PVR_FWHEAP_STATE_OFF, &image->state,
	    sizeof(image->state));
	memcpy(bytes + PVR_FWHEAP_TIMELINE_OFF, &image->timeline,
	    sizeof(image->timeline));
	bus_dmamap_sync(dmat, heap->heap.map, 0, heap->heap.size,
	    BUS_DMASYNC_PREWRITE | (heap->profile == PVR_FW_PROFILE_GE8300 ?
	    BUS_DMASYNC_PREREAD : 0));
	heap->fw_addr = PVR_FWCOMPUTE_SPAN_BASE;
	heap->prepared = true;
	*out = heap;
	return 0;
fail:
	if (heap->profile == PVR_FW_PROFILE_GE8300) {
		if (heap->heap.compute_owner != NULL)
			(void)pvr_fwmem_compute_free(mem, &heap->heap);
	} else
		pvr_heap_dma_free(dmat, &heap->heap);
	kmem_free(heap, sizeof(*heap));
	return error;
}

int
pvr_fwheap_map(struct pvr_fwheap *heap,
    struct pvr_fwmem *mem)
{
	uint64_t pages[PVR_FWHEAP_PAGES];
	size_t i;
	int error;

	if (heap == NULL || mem == NULL || !heap->prepared || heap->mapped ||
	    !fwheap_dma_valid(&heap->heap) || !fwheap_table_valid(&mem->table))
		return EINVAL;
	if (heap->profile != mem->profile ||
	    (heap->profile == PVR_FW_PROFILE_GE8300 && heap->owner != mem))
		return EINVAL;
	if (heap->profile == PVR_FW_PROFILE_GE8300) {
		if (heap->heap.device_exposed)
			return EBUSY;
		/* The live firmware page table may be visible after any write. */
		heap->heap.device_exposed = true;
	}
	for (i = 0; i < PVR_FWHEAP_PAGES; i++)
		pages[i] = heap->heap.address + i * 4096;
	error = pvr_mips_map(mem->table.cpu, mem->table.size,
	    heap->fw_addr, pages, PVR_FWHEAP_PAGES);
	if (error != 0)
		return error;
	bus_dmamap_sync(mem->dmat, mem->table.map, 0, mem->table.size,
	    BUS_DMASYNC_PREWRITE);
	heap->mapped = true;
	return 0;
}

int
pvr_fwheap_set_vm_context(struct pvr_fwheap *heap,
    const uint8_t context[32])
{

	if (heap == NULL || !heap->prepared || !fwheap_dma_valid(&heap->heap))
		return EINVAL;
	memcpy((uint8_t *)heap->heap.cpu + PVR_FWHEAP_VM_CONTEXT_OFF,
	    context, 32);
	bus_dmamap_sync(heap->dmat, heap->heap.map,
	    PVR_FWHEAP_VM_CONTEXT_OFF, 32, BUS_DMASYNC_PREWRITE);
	return 0;
}

void
pvr_fwheap_observe(const struct pvr_fwheap *heap,
    uint32_t *read_offset, uint32_t *write_offset, uint32_t *timeline)
{
	const uint8_t *bytes = heap->heap.cpu;

	bus_dmamap_sync(heap->dmat, heap->heap.map, PVR_FWHEAP_CTL_OFF,
	    sizeof(struct rogue_fwif_cccb_ctl), BUS_DMASYNC_POSTREAD);
	bus_dmamap_sync(heap->dmat, heap->heap.map,
	    PVR_FWHEAP_TIMELINE_OFF, sizeof(uint32_t), BUS_DMASYNC_POSTREAD);
	if (read_offset != NULL)
		*read_offset = *(const volatile uint32_t *)(bytes +
		    PVR_FWHEAP_CTL_OFF +
		    offsetof(struct rogue_fwif_cccb_ctl, read_offset));
	if (write_offset != NULL)
		*write_offset = *(const volatile uint32_t *)(bytes +
		    PVR_FWHEAP_CTL_OFF +
		    offsetof(struct rogue_fwif_cccb_ctl, write_offset));
	if (timeline != NULL)
		*timeline = *(const volatile uint32_t *)(bytes +
		    PVR_FWHEAP_TIMELINE_OFF);
	bus_dmamap_sync(heap->dmat, heap->heap.map, PVR_FWHEAP_CTL_OFF,
	    sizeof(struct rogue_fwif_cccb_ctl), BUS_DMASYNC_PREREAD);
	bus_dmamap_sync(heap->dmat, heap->heap.map,
	    PVR_FWHEAP_TIMELINE_OFF, sizeof(uint32_t), BUS_DMASYNC_PREREAD);
}

int
pvr_fwheap_release(struct pvr_fwheap **out)
{
	struct pvr_fwheap *heap;
	int error;

	if (out == NULL || *out == NULL)
		return EINVAL;
	heap = *out;
	if (heap->profile != PVR_FW_PROFILE_GE8300 || heap->owner == NULL)
		return EINVAL;
	error = pvr_fwmem_compute_free(heap->owner, &heap->heap);
	if (error != 0)
		return error;
	kmem_free(heap, sizeof(*heap));
	*out = NULL;
	return 0;
}

void
pvr_fwheap_destroy(struct pvr_fwheap **out)
{
	struct pvr_fwheap *heap;

	if (out == NULL || *out == NULL)
		return;
	heap = *out;
	if (heap->profile == PVR_FW_PROFILE_GE8300) {
		(void)pvr_fwheap_release(out);
		return;
	}
	if (heap->mapped) {
		/* Device-visible firmware mappings stay until hardware reset. */
		*out = NULL;
		return;
	}
	pvr_heap_dma_free(heap->dmat, &heap->heap);
	kmem_free(heap, sizeof(*heap));
	*out = NULL;
}

/* Allocate one compute backing and reject overlap with every owned page. */
static int
fwvm_alloc(struct pvr_fwvm *vm, size_t size, struct pvr_dma *dma)
{
	const struct pvr_dma *other;
	size_t i;
	int error;

	if (vm->heap->profile != PVR_FW_PROFILE_GE8300)
		return pvr_heap_dma_alloc_coherent(vm->dmat, size, dma);
	error = pvr_fwmem_compute_alloc(vm->heap->owner, size, dma);
	if (error != 0)
		return error;
	if (fwheap_dma_overlap(dma, &vm->heap->heap))
		goto overlap;
	for (i = 0; i < PVR_FWCOMPUTE_RESOURCES; i++) {
		other = &vm->resource[i];
		if (other != dma && other->loaded && fwheap_dma_overlap(dma, other))
			goto overlap;
	}
	other = &vm->catalog;
	if (other != dma && other->loaded && fwheap_dma_overlap(dma, other))
		goto overlap;
	for (i = 0; i < PVR_FWCOMPUTE_VM_WINDOWS; i++) {
		other = &vm->directory[i];
		if (other != dma && other->loaded && fwheap_dma_overlap(dma, other))
			goto overlap;
		other = &vm->table[i];
		if (other != dma && other->loaded && fwheap_dma_overlap(dma, other))
			goto overlap;
	}
	/* All parent and sibling exclusions precede initialization. */
	memset(dma->cpu, 0, dma->size);
	return 0;
overlap:
	(void)pvr_fwmem_compute_free(vm->heap->owner, dma);
	return EEXIST;
}

static void
fwvm_expose(struct pvr_fwvm *vm)
{
	size_t i;

	vm->device_exposed = true;
	for (i = 0; i < PVR_FWCOMPUTE_RESOURCES; i++)
		vm->resource[i].device_exposed = true;
	vm->catalog.device_exposed = true;
	for (i = 0; i < PVR_FWCOMPUTE_VM_WINDOWS; i++) {
		vm->directory[i].device_exposed = true;
		vm->table[i].device_exposed = true;
	}
}

int
pvr_fwvm_prepare(bus_dma_tag_t dmat,
    const struct pvr_fwcompute_file *file, const uint8_t *file_bytes,
    struct pvr_fwheap *heap,
    struct pvr_fwvm **out)
{
	struct pvr_fwcompute_vm_region regions[PVR_FWCOMPUTE_VM_REGIONS];
	struct pvr_fwcompute_vm_summary summary;
	struct pvr_fwvm *vm;
	void *directory[PVR_FWCOMPUTE_VM_WINDOWS], *table[PVR_FWCOMPUTE_VM_WINDOWS];
	uint64_t directory_pa[PVR_FWCOMPUTE_VM_WINDOWS], table_pa[PVR_FWCOMPUTE_VM_WINDOWS];
	uint8_t context[32];
	const uint8_t *payload;
	size_t i, j;
	uint32_t region_count = PVR_FWCOMPUTE_RESOURCES;
	int error;

	if (out == NULL || *out != NULL || dmat == NULL || file == NULL ||
	    file_bytes == NULL || heap == NULL || !heap->mapped)
		return EINVAL;
	if (heap->profile == PVR_FW_PROFILE_GE8300 &&
	    (heap->owner == NULL || dmat != heap->owner->dmat))
		return EINVAL;
	payload = file_bytes + file->header_bytes;
	vm = kmem_zalloc(sizeof(*vm), KM_SLEEP);
	vm->dmat = dmat;
	vm->heap = heap;
	for (i = 0; i < PVR_FWCOMPUTE_RESOURCES; i++) {
		const struct pvr_fwcompute_file_entry *e =
		    &file->resource[i];
		size_t size = ((size_t)e->bytes + 4095) & ~4095U;

		error = fwvm_alloc(vm, size, &vm->resource[i]);
		if (error != 0)
			goto fail;
		if (!fwheap_dma_valid(&vm->resource[i])) {
			error = EINVAL;
			goto fail;
		}
		for (j = 0; j < i; j++) {
			if (fwheap_dma_overlap(&vm->resource[i],
			    &vm->resource[j])) {
				error = EINVAL;
				goto fail;
			}
		}
		memcpy(vm->resource[i].cpu, payload + e->offset, e->bytes);
		bus_dmamap_sync(dmat, vm->resource[i].map, 0,
		    vm->resource[i].size, BUS_DMASYNC_PREWRITE |
		    (heap->profile == PVR_FW_PROFILE_GE8300 &&
		    (e->flags & PVR_FWCOMPUTE_FLAG_RO) == 0 ?
		    BUS_DMASYNC_PREREAD : 0));
		/* CR_CDM_CONTEXT_PDS0-style registers address only 32 bits;
		 * the context-switch PDS programs need a low-window mapping. */
		regions[i].va = e->va;
		regions[i].pa = vm->resource[i].address;
		regions[i].pages = (uint32_t)(size / 4096);
		regions[i].flags = ((e->flags & PVR_FWCOMPUTE_FLAG_RO) != 0 ?
		    PVR_FWCOMPUTE_VM_RO : 0) |
		    ((e->flags & PVR_FWCOMPUTE_FLAG_BYPASS) != 0 ?
		    PVR_FWCOMPUTE_VM_BYPASS : 0);
	}
	/* The stream resolves PDS offsets against the context-state base
	 * while the context-PDS CR registers need the low window: add a
	 * dual mapping of the PDS program page. */
	if (heap->profile == PVR_FW_PROFILE_A733) {
		uint32_t k;

		for (k = 0; k < PVR_FWCOMPUTE_RESOURCES; k++) {
			if (file->resource[k].role ==
			    PVR_FWCOMPUTE_PDS_PROGRAMS)
				break;
		}
		if (k == PVR_FWCOMPUTE_RESOURCES) {
			error = EINVAL;
			goto fail;
		}
		regions[PVR_FWCOMPUTE_RESOURCES + 0].va = UINT64_C(0x1000a000);
		regions[PVR_FWCOMPUTE_RESOURCES + 0].pa = vm->resource[k].address;
		regions[PVR_FWCOMPUTE_RESOURCES + 0].pages = regions[k].pages;
		regions[PVR_FWCOMPUTE_RESOURCES + 0].flags = regions[k].flags;
	}
	/*
	 * Keep the inherited diagnostic USC aliases for this packet change.
	 * Their removal needs proof from the compiler-produced addresses.
	 * The observed 0x2400 write fault does not prove an instruction fetch.
	 */
	if (heap->profile == PVR_FW_PROFILE_A733) {
		uint32_t k;

		for (k = 0; k < PVR_FWCOMPUTE_RESOURCES; k++) {
			if (file->resource[k].role ==
			    PVR_FWCOMPUTE_USC_PROGRAMS)
				break;
		}
		if (k == PVR_FWCOMPUTE_RESOURCES) {
			error = EINVAL;
			goto fail;
		}
		regions[PVR_FWCOMPUTE_RESOURCES + 1].va = UINT64_C(0x2000);
		regions[PVR_FWCOMPUTE_RESOURCES + 1].pa = vm->resource[k].address;
		regions[PVR_FWCOMPUTE_RESOURCES + 1].pages = regions[k].pages;
		regions[PVR_FWCOMPUTE_RESOURCES + 1].flags = regions[k].flags;
		/* Keep the inherited high alias under the same ownership. */
		regions[PVR_FWCOMPUTE_RESOURCES + 2].va = UINT64_C(0xe000002000);
		regions[PVR_FWCOMPUTE_RESOURCES + 2].pa = vm->resource[k].address;
		regions[PVR_FWCOMPUTE_RESOURCES + 2].pages = regions[k].pages;
		regions[PVR_FWCOMPUTE_RESOURCES + 2].flags = regions[k].flags;
	}
	if (heap->profile == PVR_FW_PROFILE_A733)
		region_count += 3;
	error = fwvm_alloc(vm, 4096, &vm->catalog);
	if (error != 0)
		goto fail;
	for (i = 0; i < PVR_FWCOMPUTE_VM_WINDOWS; i++) {
		error = fwvm_alloc(vm, 4096, &vm->directory[i]);
		if (error != 0)
			goto fail;
		error = fwvm_alloc(vm, 4096, &vm->table[i]);
		if (error != 0)
			goto fail;
	}
	if (!fwheap_dma_valid(&vm->catalog)) {
		error = EINVAL;
		goto fail;
	}
	for (i = 0; i < PVR_FWCOMPUTE_VM_WINDOWS; i++) {
		if (!fwheap_dma_valid(&vm->directory[i]) ||
		    !fwheap_dma_valid(&vm->table[i])) {
			error = EINVAL;
			goto fail;
		}
		directory[i] = vm->directory[i].cpu;
		table[i] = vm->table[i].cpu;
		directory_pa[i] = vm->directory[i].address;
		table_pa[i] = vm->table[i].address;
	}
	memset(&summary, 0, sizeof(summary));
	error = pvr_fwcompute_vm_encode(regions,
	    region_count,
	    vm->catalog.cpu, directory, table, directory_pa, table_pa,
	    vm->catalog.address, context, &summary);
	if (error != 0)
		goto fail;
	bus_dmamap_sync(dmat, vm->catalog.map, 0, 4096, BUS_DMASYNC_PREWRITE);
	for (i = 0; i < PVR_FWCOMPUTE_VM_WINDOWS; i++) {
		bus_dmamap_sync(dmat, vm->directory[i].map, 0, 4096,
		    BUS_DMASYNC_PREWRITE);
		bus_dmamap_sync(dmat, vm->table[i].map, 0, 4096,
		    BUS_DMASYNC_PREWRITE);
	}
	if (heap->profile == PVR_FW_PROFILE_GE8300) {
		fwvm_expose(vm);
		*out = vm;
	}
	error = pvr_fwheap_set_vm_context(heap, context);
	if (error != 0)
		goto fail;
	vm->catalog_address = vm->catalog.address;
	vm->windows = summary.windows;
	vm->directories = summary.directories;
	vm->leaves = summary.leaves;
	vm->prepared = true;
	*out = vm;
	return 0;
fail:
	if (vm->device_exposed)
		return error;
	if (heap->profile == PVR_FW_PROFILE_GE8300) {
		(void)pvr_fwvm_release(&vm);
		return error;
	}
	for (i = 0; i < PVR_FWCOMPUTE_RESOURCES; i++)
		pvr_heap_dma_free(dmat, &vm->resource[i]);
	pvr_heap_dma_free(dmat, &vm->catalog);
	for (i = 0; i < PVR_FWCOMPUTE_VM_WINDOWS; i++) {
		pvr_heap_dma_free(dmat, &vm->directory[i]);
		pvr_heap_dma_free(dmat, &vm->table[i]);
	}
	kmem_free(vm, sizeof(*vm));
	return error;
}

int
pvr_fwvm_release(struct pvr_fwvm **out)
{
	struct pvr_fwvm *vm;
	size_t i;

	if (out == NULL || *out == NULL)
		return EINVAL;
	vm = *out;
	if (vm->heap == NULL || vm->heap->owner == NULL ||
	    vm->heap->profile != PVR_FW_PROFILE_GE8300)
		return EINVAL;
	if (vm->device_exposed)
		return EBUSY;
	/* Check every child before releasing any, so failure is atomic. */
	for (i = 0; i < PVR_FWCOMPUTE_RESOURCES; i++) {
		if (vm->resource[i].device_exposed ||
		    (vm->resource[i].loaded &&
		    vm->resource[i].compute_owner == NULL) ||
		    (vm->resource[i].compute_owner != NULL &&
		    vm->resource[i].compute_owner != vm->heap->owner))
			return EBUSY;
	}
	if (vm->catalog.device_exposed ||
	    (vm->catalog.loaded && vm->catalog.compute_owner == NULL) ||
	    (vm->catalog.compute_owner != NULL &&
	    vm->catalog.compute_owner != vm->heap->owner))
		return EBUSY;
	for (i = 0; i < PVR_FWCOMPUTE_VM_WINDOWS; i++) {
		if (vm->directory[i].device_exposed || vm->table[i].device_exposed ||
		    (vm->directory[i].loaded &&
		    vm->directory[i].compute_owner == NULL) ||
		    (vm->table[i].loaded && vm->table[i].compute_owner == NULL) ||
		    (vm->directory[i].compute_owner != NULL &&
		    vm->directory[i].compute_owner != vm->heap->owner) ||
		    (vm->table[i].compute_owner != NULL &&
		    vm->table[i].compute_owner != vm->heap->owner))
			return EBUSY;
	}
	for (i = 0; i < PVR_FWCOMPUTE_RESOURCES; i++) {
		if (vm->resource[i].compute_owner != NULL)
			(void)pvr_fwmem_compute_free(vm->heap->owner, &vm->resource[i]);
	}
	if (vm->catalog.compute_owner != NULL)
		(void)pvr_fwmem_compute_free(vm->heap->owner, &vm->catalog);
	for (i = 0; i < PVR_FWCOMPUTE_VM_WINDOWS; i++) {
		if (vm->directory[i].compute_owner != NULL)
			(void)pvr_fwmem_compute_free(vm->heap->owner, &vm->directory[i]);
		if (vm->table[i].compute_owner != NULL)
			(void)pvr_fwmem_compute_free(vm->heap->owner, &vm->table[i]);
	}
	kmem_free(vm, sizeof(*vm));
	*out = NULL;
	return 0;
}

void
pvr_fwvm_destroy(struct pvr_fwvm **out)
{
	struct pvr_fwvm *vm;
	size_t i;

	if (out == NULL || *out == NULL)
		return;
	vm = *out;
	if (vm->heap != NULL &&
	    vm->heap->profile == PVR_FW_PROFILE_GE8300) {
		(void)pvr_fwvm_release(out);
		return;
	}
	for (i = 0; i < PVR_FWCOMPUTE_RESOURCES; i++)
		pvr_heap_dma_free(vm->dmat, &vm->resource[i]);
	pvr_heap_dma_free(vm->dmat, &vm->catalog);
	for (i = 0; i < PVR_FWCOMPUTE_VM_WINDOWS; i++) {
		pvr_heap_dma_free(vm->dmat, &vm->directory[i]);
		pvr_heap_dma_free(vm->dmat, &vm->table[i]);
	}
	kmem_free(vm, sizeof(*vm));
	*out = NULL;
}
