/* SPDX-License-Identifier: MIT */
/*
 * Copyright (c) 2023 Imagination Technologies Ltd.
 * Copyright (c) 2026 EmberBSD contributors.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * GPU page tables and firmware memory context: Linux PowerVR driver,
 * 7b63ef2d55f24519e7e9e5f4d15dbea03f126e40, MIT license option.
 * Native CPU-owned allocation: EmberBSD, AI-assisted. See a733-gpu-vm.md.
 */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/systm.h>
#include <arm/sunxi/sun60i_a733_gpu_vm.h>
#include <arm/sunxi/powervr/pvr_rogue_fwif.h>

#define VM_PAGE_SIZE	A733_GPU_MMU_PAGE_SIZE
#define VM_BVNC		UINT64_C(0x00240038006800b7)

__CTASSERT(sizeof(struct rogue_fwif_fwmemcontext) == 32);
__CTASSERT(offsetof(struct rogue_fwif_fwmemcontext, pc_dev_paddr) == 0);
__CTASSERT(offsetof(struct rogue_fwif_fwmemcontext,
    page_cat_base_reg_set) == 8);

static bool
vm_span(const void *p, size_t size, size_t alignment)
{
	uintptr_t address = (uintptr_t)p;

	return p != NULL && address % alignment == 0 &&
	    size - 1 <= (uintptr_t)-1 - address;
}

static bool
vm_overlap(const void *a, size_t asize, const void *b, size_t bsize)
{
	uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;

	return x <= y ? y - x < asize : x - y < bsize;
}

static void
vm_dma_free(bus_dma_tag_t tag, struct sun60i_a733_gpu_vm_dma *dma)
{
	if (dma->loaded)
		bus_dmamap_unload(tag, dma->map);
	if (dma->map != NULL)
		bus_dmamap_destroy(tag, dma->map);
	if (dma->cpu != NULL)
		bus_dmamem_unmap(tag, dma->cpu, VM_PAGE_SIZE);
	if (dma->nsegments != 0)
		bus_dmamem_free(tag, &dma->segment, dma->nsegments);
	memset(dma, 0, sizeof(*dma));
}

static void
vm_free(struct sun60i_a733_gpu_vm *vm)
{
	unsigned int i = A733_GPU_VM_PAGE_COUNT;

	while (i-- != 0)
		vm_dma_free(vm->dmat, &vm->page[i]);
	kmem_free(vm, sizeof(*vm));
}

static int
vm_dma_alloc(bus_dma_tag_t tag, struct sun60i_a733_gpu_vm_dma *dma)
{
	int error;

	error = bus_dmamem_alloc(tag, VM_PAGE_SIZE, VM_PAGE_SIZE, 0,
	    &dma->segment, 1, &dma->nsegments, BUS_DMA_WAITOK);
	if (error != 0) {
		dma->nsegments = 0;
		return error;
	}
	if (dma->nsegments != 1)
		return EINVAL;
	error = bus_dmamem_map(tag, &dma->segment, 1, VM_PAGE_SIZE,
	    &dma->cpu, BUS_DMA_WAITOK | BUS_DMA_COHERENT);
	if (error != 0) {
		dma->cpu = NULL;
		return error;
	}
	error = bus_dmamap_create(tag, VM_PAGE_SIZE, 1, VM_PAGE_SIZE, 0,
	    BUS_DMA_WAITOK, &dma->map);
	if (error != 0) {
		dma->map = NULL;
		return error;
	}
	error = bus_dmamap_load(tag, dma->map, dma->cpu, VM_PAGE_SIZE, NULL,
	    BUS_DMA_WAITOK);
	if (error != 0)
		return error;
	dma->loaded = true;
	if ((dma->map->_dm_flags & (_BUS_DMAMAP_COHERENT |
	    _BUS_DMAMAP_IS_BOUNCING)) != _BUS_DMAMAP_COHERENT)
		return EOPNOTSUPP;
	dma->coherent = true;
	if (dma->map->dm_nsegs != 1 || dma->map->dm_mapsize != VM_PAGE_SIZE ||
	    dma->map->dm_segs[0].ds_len != VM_PAGE_SIZE)
		return EINVAL;
	dma->address = dma->map->dm_segs[0].ds_addr;
	if ((dma->address & (VM_PAGE_SIZE - 1)) != 0 ||
	    dma->address > (UINT64_C(1) << 36) - VM_PAGE_SIZE ||
	    !vm_span(dma->cpu, VM_PAGE_SIZE, VM_PAGE_SIZE))
		return EINVAL;
	return 0;
}

static void
vm_put32(uint8_t *out, uint32_t value)
{
	unsigned int i;

	for (i = 0; i < 4; i++)
		out[i] = (uint8_t)(value >> (8 * i));
}

static void
vm_put64(uint8_t *out, uint64_t value)
{
	unsigned int i;

	for (i = 0; i < 8; i++)
		out[i] = (uint8_t)(value >> (8 * i));
}

int
sun60i_a733_gpu_vm_prepare(bus_dma_tag_t tag,
    const struct sun60i_a733_gpu_fwinfo *info, uint64_t scratch_va,
    struct sun60i_a733_gpu_vm **out)
{
	struct sun60i_a733_gpu_vm *vm;
	struct sun60i_a733_gpu_mmu_indices indices;
	uint64_t pa_bits, va_bits, pde, pte;
	uint32_t pce;
	unsigned int i, j;
	int error;

	if (tag == NULL || out == NULL || info == NULL ||
	    !vm_span(out, sizeof(*out), _Alignof(struct sun60i_a733_gpu_vm *)) ||
	    !vm_span(info, sizeof(*info), _Alignof(struct sun60i_a733_gpu_fwinfo)) ||
	    vm_overlap(out, sizeof(*out), info, sizeof(*info)) ||
	    scratch_va < A733_GPU_VM_GENERAL_BASE ||
	    scratch_va > A733_GPU_VM_GENERAL_END - VM_PAGE_SIZE ||
	    (scratch_va & (VM_PAGE_SIZE - 1)) != 0)
		return EINVAL;
	if (*out != NULL)
		return EBUSY;
	if (info->bvnc != VM_BVNC || info->page_size != VM_PAGE_SIZE ||
	    sun60i_a733_gpu_fwinfo_feature(info,
	    A733_GPU_FW_FEATURE_PHYS_BUS_WIDTH, &pa_bits) != 0 || pa_bits != 36 ||
	    sun60i_a733_gpu_fwinfo_feature(info,
	    A733_GPU_FW_FEATURE_VIRTUAL_ADDRESS_SPACE_BITS, &va_bits) != 0 ||
	    va_bits != 40)
		return EOPNOTSUPP;
	error = sun60i_a733_gpu_mmu_indices(scratch_va, &indices);
	if (error != 0)
		return error;
	vm = kmem_zalloc(sizeof(*vm), KM_SLEEP);
	vm->dmat = tag;
	for (i = 0; i < A733_GPU_VM_PAGE_COUNT; i++) {
		struct sun60i_a733_gpu_vm_dma *dma = &vm->page[i];

		error = vm_dma_alloc(tag, dma);
		if (error != 0)
			goto fail;
		/* Validate backend spans before writing any allocated page. */
		if (vm_overlap(dma->cpu, VM_PAGE_SIZE, info, sizeof(*info)) ||
		    vm_overlap(dma->cpu, VM_PAGE_SIZE, out, sizeof(*out)) ||
		    vm_overlap(dma->cpu, VM_PAGE_SIZE, vm, sizeof(*vm))) {
			error = EINVAL;
			goto fail;
		}
		for (j = 0; j < i; j++) {
			const struct sun60i_a733_gpu_vm_dma *prev = &vm->page[j];

			if (dma->map == prev->map ||
			    vm_overlap(dma->cpu, VM_PAGE_SIZE, prev->cpu, VM_PAGE_SIZE) ||
			    (dma->address < prev->address + VM_PAGE_SIZE &&
			    prev->address < dma->address + VM_PAGE_SIZE)) {
				error = EINVAL;
				goto fail;
			}
		}
	}
	error = sun60i_a733_gpu_mmu_pce(vm->page[A733_GPU_VM_PD].address, &pce);
	if (error != 0)
		goto fail;
	error = sun60i_a733_gpu_mmu_pde(vm->page[A733_GPU_VM_PT].address, &pde);
	if (error != 0)
		goto fail;
	error = sun60i_a733_gpu_mmu_pte(vm->page[A733_GPU_VM_SCRATCH].address,
	    0, &pte);
	if (error != 0)
		goto fail;
	for (i = 0; i < A733_GPU_VM_PAGE_COUNT; i++)
		memset(vm->page[i].cpu, 0, VM_PAGE_SIZE);
	vm_put32((uint8_t *)vm->page[A733_GPU_VM_PC].cpu + indices.pc * 4, pce);
	vm_put64((uint8_t *)vm->page[A733_GPU_VM_PD].cpu + indices.pd * 8, pde);
	vm_put64((uint8_t *)vm->page[A733_GPU_VM_PT].cpu + indices.pt * 8, pte);
	vm_put64(vm->page[A733_GPU_VM_FW_CONTEXT].cpu,
	    vm->page[A733_GPU_VM_PC].address);
	vm_put32((uint8_t *)vm->page[A733_GPU_VM_FW_CONTEXT].cpu + 8,
	    ROGUE_FW_BIF_INVALID_PCSET);
	vm->indices = indices;
	vm->scratch_va = scratch_va;
	vm->prepared = true;
	*out = vm;
	return 0;
fail:
	vm_free(vm);
	return error;
}

int
sun60i_a733_gpu_vm_retain(struct sun60i_a733_gpu_vm *vm)
{
	if (vm == NULL ||
	    !vm_span(vm, sizeof(*vm), _Alignof(struct sun60i_a733_gpu_vm)) ||
	    !vm->prepared)
		return EINVAL;
	if (vm->retained)
		return EBUSY;
	vm->retained = true;
	return 0;
}

int
sun60i_a733_gpu_vm_destroy(struct sun60i_a733_gpu_vm **out)
{
	struct sun60i_a733_gpu_vm *vm;
	unsigned int i;

	if (out == NULL)
		return 0;
	if (!vm_span(out, sizeof(*out), _Alignof(struct sun60i_a733_gpu_vm *)))
		return EINVAL;
	vm = *out;
	if (vm == NULL)
		return 0;
	if (!vm_span(vm, sizeof(*vm), _Alignof(struct sun60i_a733_gpu_vm)) ||
	    !vm->prepared)
		return EINVAL;
	if (vm->retained)
		return EBUSY;
	if (vm_overlap(out, sizeof(*out), vm, sizeof(*vm)))
		return EINVAL;
	for (i = 0; i < A733_GPU_VM_PAGE_COUNT; i++) {
		if (vm_overlap(out, sizeof(*out), vm->page[i].cpu, VM_PAGE_SIZE))
			return EINVAL;
	}
	vm_free(vm);
	*out = NULL;
	return 0;
}
