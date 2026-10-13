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
 * Firmware boot wait reference: Linux PowerVR driver,
 * 7b63ef2d55f24519e7e9e5f4d15dbea03f126e40, MIT license option.
 * Native DMA handoff: EmberBSD, AI-assisted. See a733-gpu-fwboot.md.
 */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <sys/time.h>

#include <arm/sunxi/sun60i_a733_gpu_fwboot.h>
#include <arm/sunxi/powervr/pvr_rogue_fwif.h>

static bool
fwboot_span(const void *p, size_t size, size_t align)
{
	uintptr_t a = (uintptr_t)p;

	return p != NULL && size != 0 && a % align == 0 &&
	    size - 1 <= (uintptr_t)-1 - a;
}

static bool
fwboot_overlap(const void *p, size_t size, const void *q, size_t qsize)
{
	uintptr_t a = (uintptr_t)p, b = (uintptr_t)q;

	return a <= b ? b - a < size : a - b < qsize;
}

static bool
fwboot_dma_valid(const struct sun60i_a733_gpu_dma *dma)
{
	if (!dma->loaded || dma->map == NULL || dma->nsegments != 1 ||
	    dma->size == 0 || dma->size > A733_GPU_FWINIT_MAX_WORK_SIZE ||
	    dma->size % 4096 != 0 || dma->address % 4096 != 0 ||
	    dma->address > (UINT64_C(1) << 36) - dma->size ||
	    !fwboot_span(dma->cpu, dma->size, 4096) ||
	    dma->map->dm_mapsize != dma->size || dma->map->dm_nsegs != 1 ||
	    dma->map->dm_segs[0].ds_addr != dma->address ||
	    dma->map->dm_segs[0].ds_len != dma->size)
		return false;
	return dma->address >= UINT64_C(0x1fc04000) ||
	    dma->address + dma->size <= UINT64_C(0x1fc00000);
}

static bool
fwboot_contains_page(const struct sun60i_a733_gpu_dma *dma, uint64_t pa)
{
	return pa % 4096 == 0 && pa >= dma->address &&
	    pa - dma->address <= dma->size - 4096;
}

static bool
fwboot_layout_valid(const struct sun60i_a733_gpu_fwmem *mem)
{
	static const size_t sizes[] = {
		sizeof(struct rogue_fwif_connection_ctl),
		sizeof(struct rogue_fwif_osinit),
		sizeof(struct rogue_fwif_sysinit)
	};
	static const uint32_t addresses[] = {
		A733_GPU_FWINIT_CONNECTION_VA,
		A733_GPU_FWINIT_OSINIT_VA,
		A733_GPU_FWINIT_SYSINIT_VA
	};
	const struct sun60i_a733_gpu_fwinit_layout *layout =
	    &mem->interface_layout;
	size_t i;

	if (layout->total_work_size != sun60i_a733_gpu_fwinit_work_size() ||
	    layout->total_work_size > mem->work.size)
		return false;
	/* The observed structures occupy three separately mapped ABI pages. */
	for (i = 0; i < __arraycount(sizes); i++) {
		const struct sun60i_a733_gpu_fwinit_region *r =
		    &layout->object[A733_GPU_FWINIT_CONNECTION + i];

		if (r->buffer != A733_GPU_FWINIT_CONFIG ||
		    r->offset != i * 4096 || r->size != sizes[i] ||
		    r->fw_addr != addresses[i] || r->offset > mem->config.size ||
		    sizes[i] > mem->config.size - r->offset)
			return false;
	}
	return true;
}

static int
fwboot_validate(const struct sun60i_a733_gpu_fwmem *mem,
    const struct sun60i_a733_gpu_fwboot_result *result)
{
	const struct sun60i_a733_gpu_dma *dma[5];
	size_t i, j;

	if (!fwboot_span(mem, sizeof(*mem),
	    _Alignof(struct sun60i_a733_gpu_fwmem)) || mem->dmat == NULL ||
	    !mem->interface_prepared || fwboot_overlap(mem, sizeof(*mem),
	    result, sizeof(*result)))
		return EINVAL;
	dma[0] = &mem->code;
	dma[1] = &mem->table;
	dma[2] = &mem->data;
	dma[3] = &mem->work;
	dma[4] = &mem->config;
	for (i = 0; i < __arraycount(dma); i++) {
		if (!fwboot_dma_valid(dma[i]) ||
		    fwboot_overlap(dma[i]->cpu, dma[i]->size, mem, sizeof(*mem)) ||
		    fwboot_overlap(dma[i]->cpu, dma[i]->size, result, sizeof(*result)))
			return EINVAL;
		for (j = 0; j < i; j++) {
			if (dma[i]->map == dma[j]->map ||
			    fwboot_overlap(dma[i]->cpu, dma[i]->size,
			    dma[j]->cpu, dma[j]->size) ||
			    (dma[i]->address < dma[j]->address + dma[j]->size &&
			    dma[j]->address < dma[i]->address + dma[i]->size))
				return EINVAL;
		}
	}
	if (mem->code.size != mem->image.info.allocation_size[A733_GPU_FW_CODE] ||
	    mem->data.size != mem->image.info.allocation_size[A733_GPU_FW_DATA] ||
	    mem->table.size != 16384 ||
	    mem->config.size != A733_GPU_FWINIT_CONFIG_SIZE ||
	    !fwboot_layout_valid(mem) ||
	    !fwboot_contains_page(&mem->code, mem->boot_code) ||
	    !fwboot_contains_page(&mem->code, mem->exception_code) ||
	    !fwboot_contains_page(&mem->data, mem->boot_data) ||
	    !fwboot_contains_page(&mem->data, mem->stack) ||
	    mem->boot_code == mem->exception_code || mem->boot_data == mem->stack)
		return EINVAL;
	return 0;
}

static void
fwboot_sync(struct sun60i_a733_gpu_fwmem *mem, bool before)
{
	const struct sun60i_a733_gpu_dma *dma[] = {
		&mem->code, &mem->table, &mem->data, &mem->work, &mem->config
	};
	int op;
	size_t i;

	for (i = 0; i < __arraycount(dma); i++) {
		op = before ? BUS_DMASYNC_PREWRITE : BUS_DMASYNC_POSTWRITE;
		if (i >= 2)
			op |= before ? BUS_DMASYNC_PREREAD : BUS_DMASYNC_POSTREAD;
		bus_dmamap_sync(mem->dmat, dma[i]->map, 0, dma[i]->size, op);
	}
}

/* Native FWIF asserts little-endian layout and alignment at compile time. */
static uint32_t
fwboot_read32(const void *p)
{
	return *(const volatile uint32_t *)p;
}

static uint64_t
fwboot_read64(const void *p)
{
	return *(const volatile uint64_t *)p;
}

static void
fwboot_observe(struct sun60i_a733_gpu_fwmem *mem,
    struct sun60i_a733_gpu_fwboot_result *result)
{
	const struct sun60i_a733_gpu_fwinit_layout *layout = &mem->interface_layout;
	const uint8_t *bytes = mem->config.cpu;
	const uint8_t *connection = bytes +
	    layout->object[A733_GPU_FWINIT_CONNECTION].offset;
	const uint8_t *sysinit = bytes +
	    layout->object[A733_GPU_FWINIT_SYSINIT].offset;
	const uint8_t *osinit = bytes +
	    layout->object[A733_GPU_FWINIT_OSINIT].offset;
	const uint8_t *checks = osinit +
	    offsetof(struct rogue_fwif_osinit, rogue_comp_checks);

	bus_dmamap_sync(mem->dmat, mem->config.map, 0, mem->config.size,
	    BUS_DMASYNC_POSTREAD);
	/* A device byte need not contain a valid C bool representation. */
	result->firmware_started = *(const volatile uint8_t *)(sysinit +
	    offsetof(struct rogue_fwif_sysinit, firmware_started));
#define READ32(base, type, field) \
	fwboot_read32((base) + offsetof(struct type, field))
#define READ64(base, type, field) \
	fwboot_read64((base) + offsetof(struct type, field))
	result->firmware_started_timestamp =
	    READ32(sysinit, rogue_fwif_sysinit, firmware_started_timestamp);
	result->fw_state =
	    READ32(connection, rogue_fwif_connection_ctl, connection_fw_state);
	result->os_state =
	    READ32(connection, rogue_fwif_connection_ctl, connection_os_state);
	result->alive_fw_token =
	    READ32(connection, rogue_fwif_connection_ctl, alive_fw_token);
	result->alive_os_token =
	    READ32(connection, rogue_fwif_connection_ctl, alive_os_token);
	result->hw_layout_version =
	    READ32(checks, rogue_fwif_compchecks, hw_bvnc.layout_version);
	result->fw_layout_version =
	    READ32(checks, rogue_fwif_compchecks, fw_bvnc.layout_version);
	result->hw_bvnc = READ64(checks, rogue_fwif_compchecks, hw_bvnc.bvnc);
	result->fw_bvnc = READ64(checks, rogue_fwif_compchecks, fw_bvnc.bvnc);
	result->fw_processor_version =
	    READ32(checks, rogue_fwif_compchecks, fw_processor_version);
	result->ddk_version = READ32(checks, rogue_fwif_compchecks, ddk_version);
	result->ddk_build = READ32(checks, rogue_fwif_compchecks, ddk_build);
	result->build_options = READ32(checks, rogue_fwif_compchecks, build_options);
	result->os_count_support = *(const volatile uint8_t *)(checks +
	    offsetof(struct rogue_fwif_compchecks, init_options.os_count_support));
	result->compchecks_updated = *(const volatile uint8_t *)(checks +
	    offsetof(struct rogue_fwif_compchecks, updated));
#undef READ32
#undef READ64
	result->polls++;
	result->snapshot_valid = true;
	/* No CPU write to config is allowed between these two sync operations. */
	bus_dmamap_sync(mem->dmat, mem->config.map, 0, mem->config.size,
	    BUS_DMASYNC_PREREAD);
}

int
sun60i_a733_gpu_fwboot(bus_space_tag_t bst, bus_space_handle_t bsh,
    struct sun60i_a733_gpu_fwmem *mem,
    struct sun60i_a733_gpu_fwboot_result *result)
{
	struct timespec start, now, elapsed;
	uint64_t remaining_ns;
	unsigned int wait_us;
	int error;

	if (!fwboot_span(result, sizeof(*result),
	    _Alignof(struct sun60i_a733_gpu_fwboot_result)))
		return EINVAL;
	if (result->attempted || result->device_owned)
		return EBUSY;
	if (bst == NULL)
		return EINVAL;
	error = fwboot_validate(mem, result);
	if (error != 0)
		return error;

	memset(result, 0, sizeof(*result));
	result->attempted = true;
	result->device_owned = true;
	fwboot_sync(mem, true);
	error = sun60i_a733_gpu_fwstart(bst, bsh, &mem->image.info,
	    mem->boot_code, mem->boot_data, mem->exception_code, &result->start);
	if (error != 0 || !result->start.touched || !result->start.released) {
		if (error == 0)
			error = EPROTO;
		if (!result->start.touched && !result->start.released) {
			fwboot_sync(mem, false);
			result->device_owned = false;
		}
		result->error = error;
		return error;
	}

	getnanouptime(&start);
	while (result->polls < A733_GPU_FWBOOT_MAX_POLLS) {
		getnanouptime(&now);
		timespecsub(&now, &start, &elapsed);
		if (elapsed.tv_sec >= A733_GPU_FWBOOT_TIMEOUT_US / 1000000)
			break;
		fwboot_observe(mem, result);
		if (result->firmware_started != 0) {
			error = result->firmware_started == 1 ? 0 : EPROTO;
			result->error = error;
			return error;
		}
		if (result->polls == A733_GPU_FWBOOT_MAX_POLLS)
			break;
		getnanouptime(&now);
		timespecsub(&now, &start, &elapsed);
		if (elapsed.tv_sec >= A733_GPU_FWBOOT_TIMEOUT_US / 1000000)
			break;
		remaining_ns = UINT64_C(5000000000) -
		    (uint64_t)elapsed.tv_sec * 1000000000 - elapsed.tv_nsec;
		wait_us = (unsigned int)(remaining_ns / 1000);
		if (wait_us > A733_GPU_FWBOOT_POLL_US)
			wait_us = A733_GPU_FWBOOT_POLL_US;
		if (wait_us != 0)
			delay(wait_us);
	}
	result->error = ETIMEDOUT;
	return ETIMEDOUT;
}
