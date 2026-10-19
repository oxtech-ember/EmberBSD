/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD production PowerVR heap contract, AI-assisted. */
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CTASSERT
#define CTASSERT(e) _Static_assert(e, #e)
#endif
#ifndef __arraycount
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#endif
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
typedef int32_t __s32;
typedef uint16_t __u16;
typedef uint32_t __u32;
typedef uint64_t __u64;
#include "pvr-uapi-body.h"

#define USER_BASE UINT64_C(0x10000)
#define QUERY_OFFSET 32
#define ARRAY_OFFSET 512
static uint8_t memory[8192];
static unsigned checks, inputs, outputs, fail_in, fail_out;

#define CHECK(e) do { checks++; if (!(e)) { \
	fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #e); abort(); \
} } while (0)

static bool
user_span(uintptr_t address, size_t size)
{

	return address >= USER_BASE && address - USER_BASE <= sizeof(memory) &&
	    size <= sizeof(memory) - (address - USER_BASE);
}

static int
copyin(const void *src, void *dst, size_t size)
{
	uintptr_t address = (uintptr_t)src;

	inputs++;
	if (inputs == fail_in || !user_span(address, size))
		return EFAULT;
	memcpy(dst, memory + (address - USER_BASE), size);
	return 0;
}

static int
copyout(const void *src, void *dst, size_t size)
{
	uintptr_t address = (uintptr_t)dst;

	outputs++;
	if (outputs == fail_out || !user_span(address, size))
		return EFAULT;
	memcpy(memory + (address - USER_BASE), src, size);
	return 0;
}

#include "pvr-heap-body.h"

static struct drm_pvr_ioctl_dev_query_args
prepare(uint32_t type, uint32_t size, uint32_t stride, uint32_t count,
    uint64_t array)
{
	struct drm_pvr_obj_array query = { stride, count, array };
	struct drm_pvr_ioctl_dev_query_args args = {
		.type = type, .size = size, .pointer = USER_BASE + QUERY_OFFSET,
	};

	memset(memory, 0xa5, sizeof(memory));
	memset(memory + QUERY_OFFSET, 0, 300);
	memcpy(memory + QUERY_OFFSET, &query, sizeof(query));
	inputs = outputs = fail_in = fail_out = 0;
	return args;
}

static struct drm_pvr_obj_array
query_result(void)
{
	struct drm_pvr_obj_array query;

	memcpy(&query, memory + QUERY_OFFSET, sizeof(query));
	return query;
}

static void
query_contract(uint32_t type, uint32_t object_size, uint32_t available)
{
	const uint32_t counts[] = { 0, 1, 2, 5, 6, 7, UINT32_MAX };
	const uint32_t sizes[] = { 16, 17, 272, 300 };
	struct drm_pvr_ioctl_dev_query_args args;
	struct drm_pvr_obj_array query;
	uint32_t stride, count, size, i, j, n;
	unsigned copy_count;

	args = prepare(type, 0, 0, 0, 0);
	args.pointer = 0;
	CHECK(pvr_heap_query(type, &args) == 0 && args.size == 16);
	CHECK(inputs == 0 && outputs == 0);
	args = prepare(type, 16, UINT32_MAX, UINT32_MAX, 0);
	CHECK(pvr_heap_query(type, &args) == 0);
	query = query_result();
	CHECK(query.count == available && query.stride == object_size);
	CHECK(query.array == 0);
	for (size = 0; size < 16; size++) {
		args = prepare(type, size, object_size, 1, USER_BASE + ARRAY_OFFSET);
		CHECK(pvr_heap_query(type, &args) == -EINVAL && args.size == size);
		CHECK(inputs == 0 && outputs == 0);
	}
	for (i = 0; i < __arraycount(sizes); i++) {
		size = sizes[i];
		args = prepare(type, size, object_size, 1, USER_BASE + ARRAY_OFFSET);
		CHECK(pvr_heap_query(type, &args) == 0 && args.size == 16);
		for (j = 16; j < size; j++)
			CHECK(memory[QUERY_OFFSET + j] == 0);
		CHECK(memory[QUERY_OFFSET - 1] == 0xa5);
		if (size > 16) {
			args = prepare(type, size, object_size, 1, USER_BASE + ARRAY_OFFSET);
			memory[QUERY_OFFSET + size - 1] = 1;
			CHECK(pvr_heap_query(type, &args) == -E2BIG);
			CHECK(args.size == size && outputs == 0);
		}
	}
	for (stride = 0; stride < object_size; stride++) {
		args = prepare(type, 16, stride, 1, USER_BASE + ARRAY_OFFSET);
		CHECK(pvr_heap_query(type, &args) == -EINVAL && outputs == 0);
		args = prepare(type, 16, stride, 0, UINT64_MAX);
		CHECK(pvr_heap_query(type, &args) ==
		    (type == DRM_PVR_DEV_QUERY_HEAP_INFO_GET ? 0 : -EINVAL));
	}
	for (i = 0; i < __arraycount(counts); i++) {
		count = counts[i];
		for (stride = object_size; stride <= object_size + 8; stride++) {
			args = prepare(type, 16, stride, count, USER_BASE + ARRAY_OFFSET);
			CHECK(pvr_heap_query(type, &args) == 0);
			query = query_result();
			n = MIN(count, available);
			CHECK(query.count == n && query.stride == stride);
			CHECK(memory[ARRAY_OFFSET - 1] == 0xa5);
			CHECK(memory[ARRAY_OFFSET + n * stride] == 0xa5);
			for (j = 0; j < n; j++) {
				uint32_t k;
				for (k = object_size; k < stride; k++)
					CHECK(memory[ARRAY_OFFSET + j * stride + k] == 0);
			}
		}
	}
	args = prepare(type, 16, object_size, 1, USER_BASE + ARRAY_OFFSET);
	args.pointer = UINT64_MAX - 7;
	CHECK(pvr_heap_query(type, &args) == -EFAULT);
	CHECK(inputs == 0 && outputs == 0 && args.size == 16);
	args = prepare(type, 16, object_size, 1, UINT64_MAX - 7);
	CHECK(pvr_heap_query(type, &args) == -EFAULT && outputs == 0);
	args = prepare(type, 16, UINT32_MAX, UINT32_MAX, UINT64_MAX - 4096);
	CHECK(pvr_heap_query(type, &args) == -EFAULT && outputs == 0);
	args = prepare(type, 16, object_size, 1, 1);
	CHECK(pvr_heap_query(type, &args) == -EFAULT);
	args = prepare(type, UINT32_MAX, object_size, 1, USER_BASE + ARRAY_OFFSET);
	CHECK(pvr_heap_query(type, &args) == -E2BIG && outputs == 0);
	/* An unmapped extension must fail before the query prefix is copied. */
	args = prepare(type, 32, object_size, 1, USER_BASE + ARRAY_OFFSET);
	args.pointer = USER_BASE + sizeof(memory) - 16;
	CHECK(pvr_heap_query(type, &args) == -EFAULT && inputs == 1);
	for (i = 1; i <= 3; i++) {
		args = prepare(type, 300, object_size, available, USER_BASE + ARRAY_OFFSET);
		fail_in = i;
		CHECK(pvr_heap_query(type, &args) == -EFAULT);
		CHECK(args.size == 300 && outputs == 0);
	}
	args = prepare(type, 300, object_size + 8, available, USER_BASE + ARRAY_OFFSET);
	CHECK(pvr_heap_query(type, &args) == 0);
	copy_count = outputs;
	for (i = 1; i <= copy_count; i++) {
		args = prepare(type, 300, object_size + 8, available,
		    USER_BASE + ARRAY_OFFSET);
		fail_out = i;
		CHECK(pvr_heap_query(type, &args) == -EFAULT);
		CHECK(args.size == 300);
	}
}

static void
profile_contract(void)
{
	const uint64_t bases[] = { UINT64_C(0x8000000000), UINT64_C(0xda00000000),
		UINT64_C(0xe000000000), UINT64_C(0xdbf0000000),
		UINT64_C(0xf200000000), UINT64_C(0xe400000000) };
	const uint64_t sizes[] = { UINT64_C(128) << 30, UINT64_C(4) << 30,
		UINT64_C(4) << 30, 0, UINT64_C(2) << 20, UINT64_C(16) << 30 };
	const struct drm_pvr_static_data_area areas[] = {
		{ 1, 0, 128, 0 }, { 3, 0, 1024, 128 }, { 2, 1, 128, 0 },
		{ 0, 1, 128, 128 }, { 2, 2, 128, 0 },
	};
	struct drm_pvr_ioctl_dev_query_args args;
	struct drm_pvr_heap heap;
	size_t i;

	CHECK(DRM_PVR_HEAP_COUNT == __arraycount(bases));
	args = prepare(DRM_PVR_DEV_QUERY_HEAP_INFO_GET, 16, 24, 6,
	    USER_BASE + ARRAY_OFFSET);
	CHECK(pvr_heap_query(args.type, &args) == 0);
	for (i = 0; i < __arraycount(bases); i++) {
		memcpy(&heap, memory + ARRAY_OFFSET + i * sizeof(heap), sizeof(heap));
		CHECK(heap.base == bases[i] && heap.size == sizes[i]);
		CHECK(heap.flags == 0 && heap.page_size_log2 == PAGE_SHIFT);
		if (sizes[i] == 0) {
			CHECK(pvr_heap_find(bases[i], 1) == NULL);
			continue;
		}
		CHECK(pvr_heap_find(bases[i], sizes[i]) != NULL);
		CHECK(pvr_heap_find(bases[i], sizes[i])->base == bases[i]);
		CHECK(pvr_heap_find(bases[i] + sizes[i] - 1, 1) != NULL);
		CHECK(pvr_heap_find(bases[i] - 1, 1) == NULL);
		CHECK(pvr_heap_find(bases[i], sizes[i] + 1) == NULL);
		CHECK(pvr_heap_find(bases[i] + sizes[i] - 1, 2) == NULL);
		CHECK(pvr_heap_find(bases[i] + sizes[i], 1) == NULL);
		CHECK(pvr_heap_find(bases[i], 0) == NULL);
		CHECK(pvr_heap_find(bases[i], UINT64_MAX) == NULL);
	}
	CHECK(pvr_heap_find(0, UINT64_MAX) == NULL);
	CHECK(pvr_heap_find(UINT64_MAX, 2) == NULL);
	CHECK(pvr_heap_find(UINT64_MAX, UINT64_MAX) == NULL);
	CHECK(pvr_heap_find(bases[0], bases[1] - bases[0] + 1) == NULL);
	CHECK(pvr_heap_find(UINT64_C(0xe1c0000000), 4096) == NULL);
	args = prepare(DRM_PVR_DEV_QUERY_STATIC_DATA_AREAS_GET, 16, 16, 5,
	    USER_BASE + ARRAY_OFFSET);
	CHECK(pvr_heap_query(args.type, &args) == 0);
	CHECK(memcmp(memory + ARRAY_OFFSET, areas, sizeof(areas)) == 0);
}

int
main(void)
{
	struct drm_pvr_ioctl_dev_query_args args = { 0 };

	CHECK(offsetof(struct drm_pvr_obj_array, array) == 8);
	CHECK(offsetof(struct drm_pvr_heap, page_size_log2) == 20);
	CHECK(offsetof(struct drm_pvr_static_data_area, offset) == 8);
	args.size = 123;
	CHECK(pvr_heap_query(UINT32_MAX, &args) == -EOPNOTSUPP);
	CHECK(args.size == 123 && args.pointer == 0);
	query_contract(DRM_PVR_DEV_QUERY_HEAP_INFO_GET, 24, 6);
	query_contract(DRM_PVR_DEV_QUERY_STATIC_DATA_AREAS_GET, 16, 5);
	profile_contract();
	printf("PASS: %u production heap checks, PAGE_SHIFT=%d\n", checks, PAGE_SHIFT);
	return 0;
}
