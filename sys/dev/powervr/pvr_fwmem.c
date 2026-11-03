/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2026 EmberBSD contributors.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Origin: EmberBSD, AI-assisted native bus_dma preparation. The MIPS heap
 * placement follows the MIT-option PowerVR pvr_fw.c and pvr_fw_mips.c at
 * Linux 7b63ef2d55f24519e7e9e5f4d15dbea03f126e40. No firmware is executed.
 */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kmem.h>
#include <sys/systm.h>
#include "pvr_fwmem.h"
#include "pvr_ge8300.h"
#include "pvr_mips.h"

#define PVR_FWMEM_PAGE 4096
#define PVR_FWMEM_MAX (256 * 1024)
#define PVR_FWMEM_TABLE 16384

static bool pvr_dma_mmio(const struct pvr_fwmem *, const struct pvr_dma *);
static bool pvr_dma_identity(const struct pvr_dma *);

static void
pvr_dma_free(bus_dma_tag_t dmat, struct pvr_dma *dma)
{
	if (dma->loaded)
		bus_dmamap_unload(dmat, dma->map);
	if (dma->map != NULL)
		bus_dmamap_destroy(dmat, dma->map);
	if (dma->cpu != NULL)
		bus_dmamem_unmap(dmat, dma->cpu, dma->size);
	if (dma->nsegments != 0)
		bus_dmamem_free(dmat, &dma->segment, dma->nsegments);
	memset(dma, 0, sizeof(*dma));
}

static int
pvr_dma_alloc(struct pvr_fwmem *mem, size_t size, bool coherent,
    struct pvr_dma *dma)
{
	bus_dma_tag_t dmat = mem->dmat;
	int error;

	if (size == 0 || size > PVR_FWMEM_MAX ||
	    (size & (PVR_FWMEM_PAGE - 1)) != 0)
		return EINVAL;
	dma->size = size;
	error = bus_dmamem_alloc(dmat, size, PVR_FWMEM_PAGE, 0,
	    &dma->segment, 1, &dma->nsegments, BUS_DMA_WAITOK);
	if (error != 0) {
		dma->nsegments = 0;
		return error;
	}
	if (dma->nsegments != 1) {
		error = EINVAL;
		goto fail;
	}
	error = bus_dmamem_map(dmat, &dma->segment, 1, size, &dma->cpu,
	    BUS_DMA_WAITOK | (coherent ? BUS_DMA_COHERENT : 0));
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
	/* The mapping hint alone does not establish direct coherent access. */
	if (coherent && (dma->map->_dm_flags &
	    (_BUS_DMAMAP_COHERENT | _BUS_DMAMAP_IS_BOUNCING)) !=
	    _BUS_DMAMAP_COHERENT) {
		error = EOPNOTSUPP;
		goto fail;
	}
	dma->coherent = coherent;
	if (dma->map->dm_nsegs != 1 ||
	    dma->map->dm_segs[0].ds_len != size) {
		error = EINVAL;
		goto fail;
	}
	dma->address = dma->map->dm_segs[0].ds_addr;
	if ((dma->address & (PVR_FWMEM_PAGE - 1)) != 0 ||
	    dma->address > (UINT64_C(1) << 36) - size) {
		error = EINVAL;
		goto fail;
	}
	if (pvr_dma_mmio(mem, dma)) {
		error = EEXIST;
		goto fail;
	}
	if (mem->profile == PVR_FW_PROFILE_GE8300) {
		if (!pvr_dma_identity(dma)) {
			error = EOPNOTSUPP;
			goto fail;
		}
	}
	memset(dma->cpu, 0, size);
	return 0;
fail:
	pvr_dma_free(dmat, dma);
	return error;
}

/* Allocation already checked the address-plus-size bounds. */
static bool
pvr_dma_overlap(const struct pvr_dma *a,
    const struct pvr_dma *b)
{
	return a->address < b->address + b->size &&
	    b->address < a->address + a->size;
}

/* No allocated RAM may alias the platform register resource. */
static bool
pvr_dma_mmio(const struct pvr_fwmem *mem, const struct pvr_dma *dma)
{
	return (dma->address < mem->register_pa + mem->register_size &&
	    mem->register_pa < dma->address + dma->size) ||
	    (mem->auxiliary_size != 0 &&
	    dma->address < mem->auxiliary_pa + mem->auxiliary_size &&
	    mem->auxiliary_pa < dma->address + dma->size);
}

/* A133 MIPS consumes system physical addresses, never bounced/translated. */
static bool
pvr_dma_identity(const struct pvr_dma *dma)
{
	return dma->map != NULL &&
	    (dma->map->_dm_flags & _BUS_DMAMAP_IS_BOUNCING) == 0 &&
	    dma->segment.ds_addr == dma->address &&
	    dma->segment.ds_len == dma->size;
}

int
pvr_fwmem_destroy(struct pvr_fwmem *mem)
{
	if (mem == NULL)
		return 0;
	if (mem->device_exposed)
		return EBUSY;
	/* No device has seen these addresses. CPU ownership needs no DMA sync. */
	pvr_dma_free(mem->dmat, &mem->config);
	pvr_dma_free(mem->dmat, &mem->work);
	pvr_dma_free(mem->dmat, &mem->table);
	pvr_dma_free(mem->dmat, &mem->data);
	pvr_dma_free(mem->dmat, &mem->code);
	kmem_free(mem, sizeof(*mem));
	return 0;
}

static int
pvr_fwmem_map(void *table, size_t table_size,
    const struct pvr_dma *dma, uint32_t va)
{
	uint64_t pages[PVR_FWMEM_MAX / PVR_FWMEM_PAGE];
	size_t count = dma->size / PVR_FWMEM_PAGE;

	if (count == 0 || count > __arraycount(pages))
		return EINVAL;
	for (size_t i = 0; i < count; i++)
		pages[i] = dma->address + i * PVR_FWMEM_PAGE;
	return pvr_mips_map(table, table_size,
	    va, pages, count);
}

int
pvr_fwmem_prepare(bus_dma_tag_t dmat, const void *bytes,
    size_t size, uint64_t bvnc, enum pvr_fw_profile profile,
    uint64_t register_pa, uint64_t register_size, uint64_t auxiliary_pa,
    uint64_t auxiliary_size, struct pvr_fwmem **out)
{
	struct pvr_fwmem *mem;
	struct pvr_fwinfo info;
	const struct pvr_fw_layout *main_code, *private_data;
	const struct pvr_fw_layout *boot, *boot_data;
	const struct pvr_fw_layout *except, *stack;
	uint64_t width, table_pages[4];
	int error;

	if (dmat == NULL || out == NULL || register_pa == 0 ||
	    register_size < 0x10000 || (register_pa & 4095) != 0 ||
	    (register_size & 4095) != 0 || register_size > (UINT64_C(1) << 36) ||
	    register_pa > (UINT64_C(1) << 36) - register_size ||
	    (auxiliary_size == 0 && auxiliary_pa != 0) ||
	    (auxiliary_size != 0 && (auxiliary_pa == 0 ||
	    auxiliary_size > (UINT64_C(1) << 36) ||
	    auxiliary_pa > (UINT64_C(1) << 36) - auxiliary_size)) ||
	    (profile == PVR_FW_PROFILE_GE8300 && auxiliary_size == 0) ||
	    (auxiliary_size != 0 && register_pa < auxiliary_pa + auxiliary_size &&
	    auxiliary_pa < register_pa + register_size))
		return EINVAL;
	if (profile == PVR_FW_PROFILE_A733)
		error = pvr_fwinfo_parse(bytes, size, bvnc, &info);
	else if (profile == PVR_FW_PROFILE_GE8300)
		error = pvr_ge8300_fwinfo_parse(bytes, size, bvnc, &info);
	else
		return EINVAL;
	if (error != 0)
		return error;
	error = pvr_fwinfo_feature(&info,
	    PVR_FW_FEATURE_PHYS_BUS_WIDTH, &width);
	if (error != 0 || width != 36)
		return EOPNOTSUPP;
	mem = kmem_zalloc(sizeof(*mem), KM_SLEEP);
	mem->dmat = dmat;
	mem->profile = profile;
	mem->register_pa = register_pa;
	mem->register_size = register_size;
	mem->auxiliary_pa = auxiliary_pa;
	mem->auxiliary_size = auxiliary_size;
	error = pvr_dma_alloc(mem,
	    info.allocation_size[PVR_FW_CODE], false, &mem->code);
	if (error != 0)
		goto fail;
	error = pvr_dma_alloc(mem,
	    info.allocation_size[PVR_FW_DATA], false, &mem->data);
	if (error != 0)
		goto fail;
	error = pvr_dma_alloc(mem, PVR_FWMEM_TABLE, false, &mem->table);
	if (error != 0)
		goto fail;
	/* Reject malformed backend results before publishing any DMA address. */
	if (pvr_dma_mmio(mem, &mem->code) ||
	    pvr_dma_mmio(mem, &mem->data) ||
	    pvr_dma_mmio(mem, &mem->table) ||
	    pvr_dma_overlap(&mem->code, &mem->data) ||
	    pvr_dma_overlap(&mem->code, &mem->table) ||
	    pvr_dma_overlap(&mem->data, &mem->table)) {
		error = EEXIST;
		goto fail;
	}
	if (profile == PVR_FW_PROFILE_GE8300 &&
	    (!pvr_dma_identity(&mem->code) || !pvr_dma_identity(&mem->data) ||
	    !pvr_dma_identity(&mem->table))) {
		error = EOPNOTSUPP;
		goto fail;
	}
	if (profile == PVR_FW_PROFILE_GE8300)
		error = pvr_ge8300_fwimage_prepare(bytes, size, bvnc,
		    mem->code.cpu, mem->code.size, mem->data.cpu, mem->data.size,
		    &mem->image);
	else
		error = pvr_fwimage_prepare(bytes, size, bvnc,
		    mem->code.cpu, mem->code.size, mem->data.cpu, mem->data.size,
		    &mem->image);
	if (error != 0)
		goto fail;
	main_code = &mem->image.section[
	    PVR_FW_MIPS_CODE - PVR_FW_MIPS_CODE];
	except = &mem->image.section[
	    PVR_FW_MIPS_EXCEPTIONS_CODE - PVR_FW_MIPS_CODE];
	boot = &mem->image.section[
	    PVR_FW_MIPS_BOOT_CODE - PVR_FW_MIPS_CODE];
	private_data = &mem->image.section[
	    PVR_FW_MIPS_PRIVATE_DATA - PVR_FW_MIPS_CODE];
	boot_data = &mem->image.section[
	    PVR_FW_MIPS_BOOT_DATA - PVR_FW_MIPS_CODE];
	stack = &mem->image.section[
	    PVR_FW_MIPS_STACK - PVR_FW_MIPS_CODE];
	/*
	 * The heap starts at CODE; PRIVATE_DATA has a fixed VA. The initial
	 * wrapper has one 4K window per remapped section and one stack page.
	 */
	if (main_code->base_addr != UINT32_C(0xc0000000) ||
	    main_code->alloc_offset != 0 || private_data->alloc_offset != 0 ||
	    boot->base_addr != UINT32_C(0xbfc00000) ||
	    boot_data->base_addr != UINT32_C(0xbfc01000) ||
	    except->base_addr != UINT32_C(0x9fc02000) ||
	    stack->base_addr != UINT32_C(0xcf600000) ||
	    mem->image.entry != (boot->base_addr | 1) ||
	    (boot->alloc_offset & (PVR_FWMEM_PAGE - 1)) != 0 ||
	    (boot_data->alloc_offset & (PVR_FWMEM_PAGE - 1)) != 0 ||
	    (except->alloc_offset & (PVR_FWMEM_PAGE - 1)) != 0 ||
	    (stack->alloc_offset & (PVR_FWMEM_PAGE - 1)) != 0 ||
	    boot->alloc_size != PVR_FWMEM_PAGE ||
	    boot_data->alloc_size != PVR_FWMEM_PAGE ||
	    except->alloc_size != PVR_FWMEM_PAGE ||
	    stack->alloc_size != PVR_FWMEM_PAGE) {
		error = EOPNOTSUPP;
		goto fail;
	}
	error = pvr_fwmem_map(mem->table.cpu, mem->table.size,
	    &mem->code, main_code->base_addr);
	if (error != 0)
		goto fail;
	error = pvr_fwmem_map(mem->table.cpu, mem->table.size,
	    &mem->data, private_data->base_addr);
	if (error != 0)
		goto fail;
	mem->boot_code = mem->code.address + boot->alloc_offset;
	mem->exception_code = mem->code.address + except->alloc_offset;
	mem->boot_data = mem->data.address + boot_data->alloc_offset;
	mem->stack = mem->data.address + stack->alloc_offset;
	for (size_t i = 0; i < __arraycount(table_pages); i++)
		table_pages[i] = mem->table.address + i * PVR_FWMEM_PAGE;
	error = pvr_mips_boot_data(
	    (uint8_t *)mem->data.cpu + boot_data->alloc_offset, 64,
	    mem->stack, mem->register_pa, table_pages);
	if (error != 0)
		goto fail;
	*out = mem;
	return 0;
fail:
	pvr_fwmem_destroy(mem);
	return error;
}

static bool
pvr_dma_ready(const struct pvr_dma *dma)
{
	return dma->loaded && dma->cpu != NULL && dma->map != NULL &&
	    dma->nsegments == 1 && dma->size != 0 &&
	    dma->size <= PVR_FWMEM_MAX &&
	    (dma->size & (PVR_FWMEM_PAGE - 1)) == 0 &&
	    (dma->address & (PVR_FWMEM_PAGE - 1)) == 0 &&
	    dma->address <= (UINT64_C(1) << 36) - dma->size;
}

static bool
pvr_dma_empty(const struct pvr_dma *dma)
{
	return !dma->loaded && !dma->coherent &&
	    dma->cpu == NULL && dma->map == NULL &&
	    dma->nsegments == 0 && dma->size == 0 && dma->address == 0;
}

int
pvr_fwmem_interface(struct pvr_fwmem *mem,
    uint64_t core_hz, uint32_t clusters, uint32_t slc_kib)
{
	static const uint32_t config_va[] = {
		PVR_FWINIT_CONNECTION_VA, PVR_FWINIT_OSINIT_VA,
		PVR_FWINIT_SYSINIT_VA
	};
	struct pvr_dma work = { 0 }, config = { 0 };
	struct pvr_fwinit_layout layout;
	const struct pvr_dma *areas[5];
	void *shadow = NULL;
	int error;

	if (mem == NULL || mem->dmat == NULL ||
	    !pvr_dma_ready(&mem->code) ||
	    !pvr_dma_ready(&mem->data) ||
	    !pvr_dma_ready(&mem->table) ||
	    mem->table.size != PVR_FWMEM_TABLE)
		return EINVAL;
	if (mem->interface_prepared || mem->device_exposed)
		return EBUSY;
	if (!pvr_dma_empty(&mem->work) ||
	    !pvr_dma_empty(&mem->config))
		return EINVAL;
	error = pvr_dma_alloc(mem,
	    pvr_fwinit_work_size(), true, &work);
	if (error != 0)
		goto fail;
	error = pvr_dma_alloc(mem, PVR_FWINIT_CONFIG_SIZE,
	    true, &config);
	if (error != 0)
		goto fail;
	areas[0] = &mem->code;
	areas[1] = &mem->data;
	areas[2] = &mem->table;
	areas[3] = &work;
	areas[4] = &config;
	for (size_t i = 0; i < __arraycount(areas); i++) {
		if (pvr_dma_mmio(mem, areas[i])) {
			error = EEXIST;
			goto fail;
		}
		if (mem->profile == PVR_FW_PROFILE_GE8300 &&
		    !pvr_dma_identity(areas[i])) {
			error = EOPNOTSUPP;
			goto fail;
		}
		for (size_t j = 0; j < i; j++) {
			if (pvr_dma_overlap(areas[i], areas[j])) {
				error = EEXIST;
				goto fail;
			}
		}
	}
	error = pvr_fwinit(work.cpu, work.size, config.cpu,
	    config.size, work.address, core_hz, clusters, slc_kib, &layout);
	if (error != 0)
		goto fail;
	shadow = kmem_alloc(PVR_FWMEM_TABLE, KM_SLEEP);
	memcpy(shadow, mem->table.cpu, PVR_FWMEM_TABLE);
	error = pvr_fwmem_map(shadow, PVR_FWMEM_TABLE, &work,
	    PVR_FWINIT_WORK_VA);
	if (error != 0)
		goto fail;
	for (size_t i = 0; i < __arraycount(config_va); i++) {
		uint64_t pa = config.address + i * PVR_FWMEM_PAGE;

		error = pvr_mips_map(shadow, PVR_FWMEM_TABLE,
		    config_va[i], &pa, 1);
		if (error != 0)
			goto fail;
	}

	/* No failure can follow. All addresses still belong to the CPU. */
	memcpy(mem->table.cpu, shadow, PVR_FWMEM_TABLE);
	kmem_free(shadow, PVR_FWMEM_TABLE);
	mem->work = work;
	mem->config = config;
	mem->interface_layout = layout;
	mem->interface_prepared = true;
	return 0;
fail:
	if (shadow != NULL)
		kmem_free(shadow, PVR_FWMEM_TABLE);
	pvr_dma_free(mem->dmat, &config);
	pvr_dma_free(mem->dmat, &work);
	return error;
}
