/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: Linux 7b63ef2d55f24519e7e9e5f4d15dbea03f126e40,
 * drivers/gpu/drm/imagination/pvr_drv.c, pvr_device.c, pvr_free_list.c,
 * pvr_rogue_defs.h, and pvr_rogue_fwif_dev_info.h.
 * This adaptation selects the MIT alternative. It is AI-assisted.
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
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include "pvr_drm.h"
#include "pvr_query.h"

/* Firmware metadata bit numbers, not the decimal BRN or ERN identifiers. */
#define PVR_BRN(n) (UINT64_C(1) << (n))
#define PVR_ERN(n) (UINT64_C(1) << (n))

int
pvr_query_profile_init(const struct pvr_query_input *in,
    struct pvr_query_profile *out)
{
	static const struct { uint32_t number; unsigned int bit; } quirks[] = {
		{ 47217, 1 }, { 49927, 4 }, { 62269, 7 },
		{ 48545, 3 }, { 51764, 6 },
	};
	static const struct { uint32_t number; unsigned int bit; } enhancements[] = {
		{ 35421, 0 }, { 42064, 3 },
	};
	struct pvr_query_profile p = { 0 };
	uint64_t partition, alloc, pending, additional, pixel, coeffs;
	const uint64_t *f;
	size_t i;

	if (in == NULL || out == NULL)
		return EINVAL;
	/* This native execution profile has one validated firmware ABI. */
	if (in->bvnc != UINT64_C(0x00240038006800b7) ||
	    in->fw_major != 1 || in->fw_minor != 1 || in->fw_build != 6976702 ||
	    (in->brn_mask & ~UINT64_C(0xfff)) != 0 ||
	    (in->ern_mask & ~UINT64_C(0xff)) != 0)
		return EOPNOTSUPP;
	f = in->feature;
	for (i = 0; i < PVR_QUERY_ROGUEXE; i++) {
		if (f[i] == 0 || f[i] > UINT32_MAX)
			return ERANGE;
	}
	if (f[PVR_QUERY_ROGUEXE] > 1 || f[PVR_QUERY_COMPUTE_OVERLAP] > 1)
		return EINVAL;
	partition = f[PVR_QUERY_MAX_PARTITIONS];
	if (f[PVR_QUERY_TILE_X] == 16 && f[PVR_QUERY_TILE_Y] == 16) {
		partition *= 256;
		if (partition > UINT32_MAX / f[PVR_QUERY_MIN_OUTPUT_REGS])
			return ERANGE;
		partition *= f[PVR_QUERY_MIN_OUTPUT_REGS];
	} else {
		partition *= 1024;
	}
	if (partition > UINT32_MAX ||
	    f[PVR_QUERY_COMMON_STORE] < partition + 1024)
		return ERANGE;
	alloc = f[PVR_QUERY_COMMON_STORE] - 1024 - partition;
	if ((in->brn_mask & PVR_BRN(0)) != 0) /* BRN44079 */
		alloc = MIN(alloc, 768 * 4 * 4 - 1024);
	p.num_phantoms = (f[PVR_QUERY_NUM_CLUSTERS] + 3) / 4;
	pixel = ((f[PVR_QUERY_TILES_IN_FLIGHT] + p.num_phantoms - 1) /
	    p.num_phantoms) * 1024;
	pending = (in->ern_mask & PVR_ERN(2)) != 0 ? 0 : 2048; /* ERN38748 */
	additional = 1024;
	if ((in->ern_mask & PVR_ERN(1)) != 0) /* ERN38020 */
		additional += 2048;
	coeffs = alloc;
	if ((in->brn_mask & PVR_BRN(2)) != 0 && !f[PVR_QUERY_COMPUTE_OVERLAP])
		coeffs += 2048; /* BRN48492 */
	if (pixel + additional + pending > coeffs)
		return ERANGE;
	coeffs -= pixel + additional + pending;
	if (coeffs > UINT32_MAX)
		return ERANGE;
	p.runtime.common_store_partition_space_size = partition;
	p.runtime.common_store_alloc_region_size = alloc;
	p.runtime.max_coeffs = coeffs;
	if ((in->brn_mask & PVR_BRN(2)) != 0 && f[PVR_QUERY_ROGUEXE] &&
	    !f[PVR_QUERY_COMPUTE_OVERLAP]) {
		if (coeffs < 2048)
			return ERANGE;
		coeffs -= 2048;
	}
	p.runtime.cdm_max_local_mem_size_regs = MIN(coeffs, 4096);
	p.runtime.free_list_min_pages = !f[PVR_QUERY_ROGUEXE] ? 50 :
	    ((in->brn_mask & PVR_BRN(10)) != 0 ? 40 : 25); /* BRN66011 */
	/* The upstream PM limit is 2 GiB in 4096-byte PM pages. */
	p.runtime.free_list_max_pages = UINT64_C(0x80000000) / 4096;
	for (i = 0; i < __arraycount(quirks); i++) {
		if ((in->brn_mask & PVR_BRN(quirks[i].bit)) == 0)
			continue;
		p.quirks[p.quirk_count++] = quirks[i].number;
		if (i < 3)
			p.musthave_count++;
	}
	for (i = 0; i < __arraycount(enhancements); i++) {
		if ((in->ern_mask & PVR_ERN(enhancements[i].bit)) != 0)
			p.enhancements[p.enhancement_count++] = enhancements[i].number;
	}
	*out = p;
	return 0;
}

CTASSERT(sizeof(struct drm_pvr_dev_query_runtime_info) == 32);
CTASSERT(sizeof(struct drm_pvr_dev_query_quirks) == 16);
CTASSERT(sizeof(struct drm_pvr_dev_query_enhancements) == 16);

static bool
pvr_query_span(uint64_t address, uint64_t size)
{

	return address <= UINTPTR_MAX && size != 0 &&
	    size - 1 <= UINTPTR_MAX - address;
}

static int
pvr_query_in(uint64_t address, uint32_t size, void *object, size_t expected)
{
	uint8_t tail[256];
	size_t offset, count, i;
	int error;

	if (size < expected)
		return -EINVAL;
	if (!pvr_query_span(address, size))
		return -EFAULT;
	for (offset = expected; offset < size; offset += count) {
		count = MIN(size - offset, sizeof(tail));
		error = copyin((const void *)(uintptr_t)(address + offset), tail, count);
		if (error != 0)
			return -error;
		for (i = 0; i < count; i++) {
			if (tail[i] != 0)
				return -E2BIG;
		}
	}
	return -copyin((const void *)(uintptr_t)address, object, expected);
}

static int
pvr_query_out(uint64_t address, uint32_t size, const void *object, size_t expected)
{
	static const uint8_t zeros[256];
	size_t offset, count;
	int error;

	if (size < expected)
		return -EINVAL;
	if (!pvr_query_span(address, size))
		return -EFAULT;
	error = copyout(object, (void *)(uintptr_t)address, expected);
	if (error != 0)
		return -error;
	for (offset = expected; offset < size; offset += count) {
		count = MIN(size - offset, sizeof(zeros));
		error = copyout(zeros, (void *)(uintptr_t)(address + offset), count);
		if (error != 0)
			return -error;
	}
	return 0;
}

int
pvr_query(const struct pvr_query_profile *profile, uint32_t type,
    struct drm_pvr_ioctl_dev_query_args *args)
{
	struct drm_pvr_dev_query_runtime_info runtime = { 0 };
	struct drm_pvr_dev_query_quirks quirks;
	struct drm_pvr_dev_query_enhancements enhancements;
	void *query;
	const uint32_t *values;
	uint64_t array;
	uint32_t size;
	uint16_t capacity, count;
	int error;

	switch (type) {
	case DRM_PVR_DEV_QUERY_RUNTIME_INFO_GET:
		size = sizeof(runtime);
		break;
	case DRM_PVR_DEV_QUERY_QUIRKS_GET:
		size = sizeof(quirks);
		break;
	case DRM_PVR_DEV_QUERY_ENHANCEMENTS_GET:
		size = sizeof(enhancements);
		break;
	default:
		return -EOPNOTSUPP;
	}
	if (args->pointer == 0) {
		args->size = size;
		return 0;
	}
	if (type == DRM_PVR_DEV_QUERY_RUNTIME_INFO_GET) {
		runtime.free_list_min_pages = profile->runtime.free_list_min_pages;
		runtime.free_list_max_pages = profile->runtime.free_list_max_pages;
		runtime.common_store_alloc_region_size =
		    profile->runtime.common_store_alloc_region_size;
		runtime.common_store_partition_space_size =
		    profile->runtime.common_store_partition_space_size;
		runtime.max_coeffs = profile->runtime.max_coeffs;
		runtime.cdm_max_local_mem_size_regs = profile->runtime.cdm_max_local_mem_size_regs;
		error = pvr_query_out(args->pointer, args->size, &runtime, size);
		if (error == 0)
			args->size = size;
		return error;
	}
	query = type == DRM_PVR_DEV_QUERY_QUIRKS_GET ? (void *)&quirks :
	    (void *)&enhancements;
	error = pvr_query_in(args->pointer, args->size, query, size);
	if (error != 0)
		return error;
	if (type == DRM_PVR_DEV_QUERY_QUIRKS_GET) {
		if (quirks._padding_c != 0)
			return -EINVAL;
		array = quirks.quirks;
		capacity = quirks.count;
		count = profile->quirk_count;
		values = profile->quirks;
		quirks.count = count;
		/* Pinned Linux updates this field only on the data-copy path. */
		if (array != 0)
			quirks.musthave_count = profile->musthave_count;
	} else {
		if (enhancements._padding_a != 0 || enhancements._padding_c != 0)
			return -EINVAL;
		array = enhancements.enhancements;
		capacity = enhancements.count;
		count = profile->enhancement_count;
		values = profile->enhancements;
		enhancements.count = count;
	}
	if (array != 0) {
		if (capacity < count)
			return -E2BIG;
		if (count != 0) {
			if (!pvr_query_span(array, count * sizeof(*values)))
				return -EFAULT;
			error = copyout(values, (void *)(uintptr_t)array,
			    count * sizeof(*values));
			if (error != 0)
				return -error;
		}
	}
	error = pvr_query_out(args->pointer, args->size, query, size);
	if (error == 0)
		args->size = size;
	return error;
}
