/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/systm.h>

#include <arm/sunxi/sun60i_a733_gpu_fwheap.h>
#include <arm/sunxi/sun60i_a733_gpu_fwcompute_file.h>
#include <arm/sunxi/sun60i_a733_gpu_fwcompute_vm.h>
#include <arm/sunxi/sun60i_a733_gpu_mips.h>

/* Same discipline as the firmware memory owner: coherent, single segment,
 * page-aligned, inside 36 bits, zeroed by the allocator. */
static int
fwheap_dma_alloc(bus_dma_tag_t dmat, size_t size,
    struct sun60i_a733_gpu_dma *dma)
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

static void
fwheap_dma_free(bus_dma_tag_t dmat, struct sun60i_a733_gpu_dma *dma)
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
fwheap_dma_valid(const struct sun60i_a733_gpu_dma *dma)
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
fwheap_table_valid(const struct sun60i_a733_gpu_dma *dma)
{

	return dma->loaded && dma->cpu != NULL && dma->map != NULL &&
	    dma->map->dm_nsegs == 1 &&
	    dma->map->dm_segs[0].ds_addr == dma->address &&
	    (dma->address & 4095) == 0 &&
	    dma->address <= (UINT64_C(1) << 36) - dma->size;
}

static bool
fwheap_dma_overlap(const struct sun60i_a733_gpu_dma *a,
    const struct sun60i_a733_gpu_dma *b)
{

	return a->address < b->address + b->size &&
	    b->address < a->address + a->size;
}

int
sun60i_a733_gpu_fwheap_prepare(bus_dma_tag_t dmat,
    struct sun60i_a733_gpu_fwmem *mem,
    const struct sun60i_a733_gpu_fwcompute_image *image,
    struct sun60i_a733_gpu_fwheap **out)
{
	struct sun60i_a733_gpu_fwheap *heap;
	uint8_t *bytes;
	int error;

	if (out == NULL || *out != NULL || dmat == NULL || mem == NULL ||
	    image == NULL || image->used_bytes != A733_GPU_FWCOMPUTE_USED_BYTES)
		return EINVAL;
	heap = kmem_zalloc(sizeof(*heap), KM_SLEEP);
	heap->dmat = dmat;
	error = fwheap_dma_alloc(dmat, A733_GPU_FWHEAP_BYTES, &heap->heap);
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
	/* The serializer's object bytes land at their firmware addresses. */
	memcpy(bytes + A733_GPU_FWHEAP_CONTEXT_OFF, &image->context,
	    sizeof(image->context));
	memcpy(bytes + A733_GPU_FWHEAP_CTL_OFF, &image->ctl,
	    sizeof(image->ctl));
	memcpy(bytes + A733_GPU_FWHEAP_CCCB_OFF, image->cccb,
	    A733_GPU_FWCOMPUTE_CCCB_SIZE);
	memcpy(bytes + A733_GPU_FWHEAP_STATE_OFF, &image->state,
	    sizeof(image->state));
	memcpy(bytes + A733_GPU_FWHEAP_TIMELINE_OFF, &image->timeline,
	    sizeof(image->timeline));
	bus_dmamap_sync(dmat, heap->heap.map, 0, heap->heap.size,
	    BUS_DMASYNC_PREWRITE);
	heap->fw_addr = A733_GPU_FWCOMPUTE_SPAN_BASE;
	heap->prepared = true;
	*out = heap;
	return 0;
fail:
	fwheap_dma_free(dmat, &heap->heap);
	kmem_free(heap, sizeof(*heap));
	return error;
}

int
sun60i_a733_gpu_fwheap_map(struct sun60i_a733_gpu_fwheap *heap,
    struct sun60i_a733_gpu_fwmem *mem)
{
	uint64_t pages[A733_GPU_FWHEAP_PAGES];
	size_t i;
	int error;

	if (heap == NULL || mem == NULL || !heap->prepared || heap->mapped ||
	    !fwheap_dma_valid(&heap->heap) || !fwheap_table_valid(&mem->table))
		return EINVAL;
	for (i = 0; i < A733_GPU_FWHEAP_PAGES; i++)
		pages[i] = heap->heap.address + i * 4096;
	error = sun60i_a733_gpu_mips_map(mem->table.cpu, mem->table.size,
	    heap->fw_addr, pages, A733_GPU_FWHEAP_PAGES);
	if (error != 0)
		return error;
	bus_dmamap_sync(mem->dmat, mem->table.map, 0, mem->table.size,
	    BUS_DMASYNC_PREWRITE);
	heap->mapped = true;
	return 0;
}

int
sun60i_a733_gpu_fwheap_set_vm_context(struct sun60i_a733_gpu_fwheap *heap,
    const uint8_t context[32])
{

	if (heap == NULL || !heap->prepared || !fwheap_dma_valid(&heap->heap))
		return EINVAL;
	memcpy((uint8_t *)heap->heap.cpu + A733_GPU_FWHEAP_VM_CONTEXT_OFF,
	    context, 32);
	bus_dmamap_sync(heap->dmat, heap->heap.map,
	    A733_GPU_FWHEAP_VM_CONTEXT_OFF, 32, BUS_DMASYNC_PREWRITE);
	return 0;
}

void
sun60i_a733_gpu_fwheap_observe(const struct sun60i_a733_gpu_fwheap *heap,
    uint32_t *read_offset, uint32_t *write_offset, uint32_t *timeline)
{
	const uint8_t *bytes = heap->heap.cpu;

	if (read_offset != NULL)
		*read_offset = *(const volatile uint32_t *)(bytes +
		    A733_GPU_FWHEAP_CTL_OFF);
	if (write_offset != NULL)
		*write_offset = *(const volatile uint32_t *)(bytes +
		    A733_GPU_FWHEAP_CTL_OFF + 4);
	if (timeline != NULL)
		*timeline = *(const volatile uint32_t *)(bytes +
		    A733_GPU_FWHEAP_TIMELINE_OFF);
}

void
sun60i_a733_gpu_fwheap_destroy(struct sun60i_a733_gpu_fwheap **out)
{
	struct sun60i_a733_gpu_fwheap *heap;

	if (out == NULL || *out == NULL)
		return;
	heap = *out;
	if (heap->mapped) {
		/* Device-visible firmware mappings stay until hardware reset. */
		*out = NULL;
		return;
	}
	fwheap_dma_free(heap->dmat, &heap->heap);
	kmem_free(heap, sizeof(*heap));
	*out = NULL;
}

int
sun60i_a733_gpu_fwvm_prepare(bus_dma_tag_t dmat,
    const struct a733_gpu_fwcompute_file *file, const uint8_t *file_bytes,
    struct sun60i_a733_gpu_fwheap *heap,
    struct sun60i_a733_gpu_fwvm **out)
{
	struct a733_gpu_fwcompute_vm_region regions[11];
	struct a733_gpu_fwcompute_vm_summary summary;
	struct sun60i_a733_gpu_fwvm *vm;
	void *directory[A733_GPU_FWCOMPUTE_VM_WINDOWS], *table[A733_GPU_FWCOMPUTE_VM_WINDOWS];
	uint64_t directory_pa[A733_GPU_FWCOMPUTE_VM_WINDOWS], table_pa[A733_GPU_FWCOMPUTE_VM_WINDOWS];
	uint8_t context[32];
	const uint8_t *payload;
	size_t i, j;
	int error;

	if (out == NULL || *out != NULL || dmat == NULL || file == NULL ||
	    file_bytes == NULL || heap == NULL || !heap->mapped)
		return EINVAL;
	payload = file_bytes + file->header_bytes;
	vm = kmem_zalloc(sizeof(*vm), KM_SLEEP);
	vm->dmat = dmat;
	for (i = 0; i < 9; i++) {
		const struct a733_gpu_fwcompute_file_entry *e =
		    &file->resource[i];
		size_t size = ((size_t)e->bytes + 4095) & ~4095U;

		error = fwheap_dma_alloc(dmat, size, &vm->resource[i]);
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
		    vm->resource[i].size, BUS_DMASYNC_PREWRITE);
		/* CR_CDM_CONTEXT_PDS0-style registers address only 32 bits;
		 * the context-switch PDS programs need a low-window mapping. */
		regions[i].va = e->va;
		regions[i].pa = vm->resource[i].address;
		regions[i].pages = (uint32_t)(size / 4096);
		regions[i].flags = ((e->flags & A733_GPU_FWCOMPUTE_FLAG_RO) != 0 ?
		    A733_GPU_FWCOMPUTE_VM_RO : 0) |
		    ((e->flags & A733_GPU_FWCOMPUTE_FLAG_BYPASS) != 0 ?
		    A733_GPU_FWCOMPUTE_VM_BYPASS : 0);
	}
	/* The stream resolves PDS offsets against the context-state base
	 * while the context-PDS CR registers need the low window: add a
	 * dual mapping of the PDS program page. */
	{
		uint32_t k;

		for (k = 0; k < 9; k++) {
			if (file->resource[k].role ==
			    A733_GPU_FWCOMPUTE_PDS_PROGRAMS)
				break;
		}
		if (k == 9) {
			error = EINVAL;
			goto fail;
		}
		regions[9].va = UINT64_C(0x1000a000);
		regions[9].pa = vm->resource[k].address;
		regions[9].pages = 1;
		regions[9].flags = regions[k].flags;
	}
	/* The generator encodes the USC execution offset in the low 32-bit
	 * space; alias the USC page so 0x2400-class fetches resolve. */
	{
		uint32_t k;

		for (k = 0; k < 9; k++) {
			if (file->resource[k].role ==
			    A733_GPU_FWCOMPUTE_USC_PROGRAMS)
				break;
		}
		if (k == 9) {
			error = EINVAL;
			goto fail;
		}
		regions[10].va = UINT64_C(0x2000);
		regions[10].pa = vm->resource[k].address;
		regions[10].pages = 1;
		regions[10].flags = regions[k].flags;
	}
	error = fwheap_dma_alloc(dmat, 4096, &vm->catalog);
	if (error != 0)
		goto fail;
	for (i = 0; i < A733_GPU_FWCOMPUTE_VM_WINDOWS; i++) {
		error = fwheap_dma_alloc(dmat, 4096, &vm->directory[i]);
		if (error != 0)
			goto fail;
		error = fwheap_dma_alloc(dmat, 4096, &vm->table[i]);
		if (error != 0)
			goto fail;
	}
	if (!fwheap_dma_valid(&vm->catalog)) {
		error = EINVAL;
		goto fail;
	}
	for (i = 0; i < A733_GPU_FWCOMPUTE_VM_WINDOWS; i++) {
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
	error = sun60i_a733_gpu_fwcompute_vm_encode(regions, 11,
	    vm->catalog.cpu, directory, table, directory_pa, table_pa,
	    vm->catalog.address, context, &summary);
	if (error != 0)
		goto fail;
	bus_dmamap_sync(dmat, vm->catalog.map, 0, 4096, BUS_DMASYNC_PREWRITE);
	for (i = 0; i < A733_GPU_FWCOMPUTE_VM_WINDOWS; i++) {
		bus_dmamap_sync(dmat, vm->directory[i].map, 0, 4096,
		    BUS_DMASYNC_PREWRITE);
		bus_dmamap_sync(dmat, vm->table[i].map, 0, 4096,
		    BUS_DMASYNC_PREWRITE);
	}
	error = sun60i_a733_gpu_fwheap_set_vm_context(heap, context);
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
	for (i = 0; i < 9; i++)
		fwheap_dma_free(dmat, &vm->resource[i]);
	fwheap_dma_free(dmat, &vm->catalog);
	for (i = 0; i < A733_GPU_FWCOMPUTE_VM_WINDOWS; i++) {
		fwheap_dma_free(dmat, &vm->directory[i]);
		fwheap_dma_free(dmat, &vm->table[i]);
	}
	kmem_free(vm, sizeof(*vm));
	return error;
}

void
sun60i_a733_gpu_fwvm_destroy(struct sun60i_a733_gpu_fwvm **out)
{
	struct sun60i_a733_gpu_fwvm *vm;
	size_t i;

	if (out == NULL || *out == NULL)
		return;
	vm = *out;
	for (i = 0; i < 9; i++)
		fwheap_dma_free(vm->dmat, &vm->resource[i]);
	fwheap_dma_free(vm->dmat, &vm->catalog);
	for (i = 0; i < A733_GPU_FWCOMPUTE_VM_WINDOWS; i++) {
		fwheap_dma_free(vm->dmat, &vm->directory[i]);
		fwheap_dma_free(vm->dmat, &vm->table[i]);
	}
	kmem_free(vm, sizeof(*vm));
	*out = NULL;
}
