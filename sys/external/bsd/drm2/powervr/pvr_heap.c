/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: Linux 7b63ef2d55f24519e7e9e5f4d15dbea03f126e40,
 * drivers/gpu/drm/imagination/pvr_vm.c, pvr_rogue_heap_config.h,
 * and the user-object helpers in pvr_drv.c and pvr_drv.h.
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
#include "pvr_heap.h"

/* GPU virtual ranges, not reservations of physical memory. */
static const struct drm_pvr_heap pvr_heaps[DRM_PVR_HEAP_COUNT] = {
	[DRM_PVR_HEAP_GENERAL] = {
		.base = UINT64_C(0x8000000000), .size = UINT64_C(0x2000000000),
		.page_size_log2 = PAGE_SHIFT,
	},
	[DRM_PVR_HEAP_PDS_CODE_DATA] = {
		.base = UINT64_C(0xda00000000), .size = UINT64_C(0x100000000),
		.page_size_log2 = PAGE_SHIFT,
	},
	[DRM_PVR_HEAP_USC_CODE] = {
		.base = UINT64_C(0xe000000000), .size = UINT64_C(0x100000000),
		.page_size_log2 = PAGE_SHIFT,
	},
	/* Mesa 26.2.4 bxm-4-64.h: this BVNC does not have BRN63142. */
	[DRM_PVR_HEAP_RGNHDR] = {
		.base = UINT64_C(0xdbf0000000), .size = 0,
		.page_size_log2 = PAGE_SHIFT,
	},
	[DRM_PVR_HEAP_VIS_TEST] = {
		.base = UINT64_C(0xf200000000), .size = UINT64_C(0x200000),
		.page_size_log2 = PAGE_SHIFT,
	},
	[DRM_PVR_HEAP_TRANSFER_FRAG] = {
		.base = UINT64_C(0xe400000000), .size = UINT64_C(0x400000000),
		.page_size_log2 = PAGE_SHIFT,
	},
};

static const struct drm_pvr_static_data_area pvr_static_areas[] = {
	{ DRM_PVR_STATIC_DATA_AREA_FENCE, DRM_PVR_HEAP_GENERAL, 128, 0 },
	{ DRM_PVR_STATIC_DATA_AREA_YUV_CSC, DRM_PVR_HEAP_GENERAL, 1024, 128 },
	{ DRM_PVR_STATIC_DATA_AREA_VDM_SYNC, DRM_PVR_HEAP_PDS_CODE_DATA, 128, 0 },
	{ DRM_PVR_STATIC_DATA_AREA_EOT, DRM_PVR_HEAP_PDS_CODE_DATA, 128, 128 },
	{ DRM_PVR_STATIC_DATA_AREA_VDM_SYNC, DRM_PVR_HEAP_USC_CODE, 128, 0 },
};

/* The pinned ABI makes every field in these objects mandatory. */
CTASSERT(sizeof(struct drm_pvr_obj_array) == 16);
CTASSERT(sizeof(struct drm_pvr_dev_query_heap_info) == 16);
CTASSERT(sizeof(struct drm_pvr_dev_query_static_data_areas) == 16);
CTASSERT(sizeof(struct drm_pvr_heap) == 24);
CTASSERT(sizeof(struct drm_pvr_static_data_area) == 16);

static bool
pvr_heap_user_span(uint64_t address, uint64_t size)
{

	return address <= UINTPTR_MAX && size != 0 &&
	    size - 1 <= UINTPTR_MAX - address;
}

static int
pvr_heap_query_in(uint64_t address, uint32_t size,
    struct drm_pvr_obj_array *query)
{
	uint8_t tail[256];
	size_t offset, count, i;
	int error;

	if (size < sizeof(*query))
		return -EINVAL;
	if (!pvr_heap_user_span(address, size))
		return -EFAULT;
	/* Match copy_struct_from_user: reject unknown nonzero input fields. */
	for (offset = sizeof(*query); offset < size; offset += count) {
		count = MIN(size - offset, sizeof(tail));
		error = copyin((const void *)(uintptr_t)(address + offset),
		    tail, count);
		if (error != 0)
			return -error;
		for (i = 0; i < count; i++) {
			if (tail[i] != 0)
				return -E2BIG;
		}
	}
	return -copyin((const void *)(uintptr_t)address, query, sizeof(*query));
}

static int
pvr_heap_query_out(uint64_t address, uint32_t size,
    const void *object, size_t object_size)
{
	static const uint8_t zeros[256];
	size_t offset, count;
	int error;

	if (!pvr_heap_user_span(address, size))
		return -EFAULT;
	error = copyout(object, (void *)(uintptr_t)address, object_size);
	if (error != 0)
		return -error;
	for (offset = object_size; offset < size; offset += count) {
		count = MIN(size - offset, sizeof(zeros));
		error = copyout(zeros, (void *)(uintptr_t)(address + offset), count);
		if (error != 0)
			return -error;
	}
	return 0;
}

int
pvr_heap_query(uint32_t type, struct drm_pvr_ioctl_dev_query_args *args)
{
	struct drm_pvr_obj_array query;
	const uint8_t *objects;
	size_t object_size;
	uint32_t count, i;
	uint64_t address;
	int error;

	switch (type) {
	case DRM_PVR_DEV_QUERY_HEAP_INFO_GET:
		objects = (const uint8_t *)pvr_heaps;
		object_size = sizeof(pvr_heaps[0]);
		count = __arraycount(pvr_heaps);
		break;
	case DRM_PVR_DEV_QUERY_STATIC_DATA_AREAS_GET:
		objects = (const uint8_t *)pvr_static_areas;
		object_size = sizeof(pvr_static_areas[0]);
		count = __arraycount(pvr_static_areas);
		break;
	default:
		return -EOPNOTSUPP;
	}
	if (args->pointer == 0) {
		args->size = sizeof(query);
		return 0;
	}
	error = pvr_heap_query_in(args->pointer, args->size, &query);
	if (error != 0)
		return error;
	if (query.array == 0) {
		query.count = count;
		query.stride = object_size;
	} else {
		query.count = MIN(query.count, count);
		/* The upstream static-array helper checks even an empty array. */
		if ((query.count != 0 ||
		    type == DRM_PVR_DEV_QUERY_STATIC_DATA_AREAS_GET) &&
		    query.stride < object_size)
			return -EINVAL;
		if (query.count != 0 && !pvr_heap_user_span(query.array,
		    (uint64_t)query.count * query.stride))
			return -EFAULT;
		address = query.array;
		for (i = 0; i < query.count; i++) {
			error = pvr_heap_query_out(address, query.stride,
			    objects + i * object_size, object_size);
			if (error != 0)
				return error;
			address += query.stride;
		}
	}
	error = pvr_heap_query_out(args->pointer, args->size, &query, sizeof(query));
	if (error != 0)
		return error;
	args->size = sizeof(query);
	return 0;
}

const struct drm_pvr_heap *
pvr_heap_find(uint64_t start, uint64_t size)
{
	const struct drm_pvr_heap *heap;
	uint64_t offset;
	size_t i;

	if (size == 0)
		return NULL;
	for (i = 0; i < __arraycount(pvr_heaps); i++) {
		heap = &pvr_heaps[i];
		if (start < heap->base)
			continue;
		offset = start - heap->base;
		if (offset < heap->size && size <= heap->size - offset)
			return heap;
	}
	return NULL;
}
