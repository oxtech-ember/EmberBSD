/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD tests of the actual metadata parser, adapter, and queries. */
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sun60i_a733_gpu_fwinfo.h"

typedef uint16_t __u16;
typedef uint32_t __u32;
typedef uint64_t __u64;
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#ifndef __arraycount
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#endif
#define CTASSERT(e) _Static_assert(e, #e)
#include "pvr-drm.h-body.h"
#include "pvr-query.h-body.h"

static unsigned int checks, copies, fail_copy;
static uint8_t arena[4096];
#define QUERY 16
#define ARRAY 2048
#define CHECK(e) do { \
	checks++; \
	if (!(e)) { \
		fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #e); \
		abort(); \
	} \
} while (0)

static int
copy_user(const void *from, void *to, size_t size, bool output)
{
	uintptr_t address = (uintptr_t)(output ? to : from);
	uintptr_t start = (uintptr_t)arena;

	copies++;
	if (copies == fail_copy || address < start || address - start > sizeof(arena) ||
	    size > sizeof(arena) - (address - start))
		return EFAULT;
	memcpy(to, from, size);
	return 0;
}

static int
copyin(const void *from, void *to, size_t size)
{
	return copy_user(from, to, size, false);
}

static int
copyout(const void *from, void *to, size_t size)
{
	return copy_user(from, to, size, true);
}

#include "pvr-query.c-body.h"
#include "adapter-body.h"

static struct pvr_query_input
metadata(const char *path)
{
	struct sun60i_a733_gpu_fwinfo info;
	struct pvr_query_input input, saved;
	uint8_t *bytes;
	FILE *file;
	long length;
	unsigned int i;

	file = fopen(path, "rb");
	CHECK(file != NULL);
	CHECK(fseek(file, 0, SEEK_END) == 0);
	length = ftell(file);
	CHECK(length == 131072);
	CHECK(fseek(file, 0, SEEK_SET) == 0);
	bytes = malloc((size_t)length);
	CHECK(bytes != NULL);
	CHECK(fread(bytes, 1, (size_t)length, file) == (size_t)length);
	CHECK(fclose(file) == 0);
	CHECK(sun60i_a733_gpu_fwinfo_parse(bytes, (size_t)length,
	    UINT64_C(0x00240038006800b7), &info) == 0);
	free(bytes);
	CHECK(sun60i_gpu_query_input(&info, &input) == 0);
	CHECK(input.bvnc == UINT64_C(0x00240038006800b7));
	CHECK(input.fw_major == 1 && input.fw_minor == 1 && input.fw_build == 6976702);
	CHECK(input.brn_mask == 0x201 && input.ern_mask == 0xf5);
	{
		const uint64_t expected[] = {19456, 12, 16, 16, 2, 1, 6, 1, 1};
		CHECK(memcmp(input.feature, expected, sizeof(expected)) == 0);
	}
	saved = input;
	for (i = 0; i < A733_GPU_FW_FEATURE_MAX; i++) {
		struct sun60i_a733_gpu_fwinfo broken = info;
		int error;

		broken.feature_mask[i / 64] &= ~(UINT64_C(1) << (i % 64));
		input = saved;
		error = sun60i_gpu_query_input(&broken, &input);
		if (i == A733_GPU_FW_FEATURE_COMMON_STORE_SIZE_IN_DWORDS ||
		    i == A733_GPU_FW_FEATURE_MAX_PARTITIONS ||
		    i == A733_GPU_FW_FEATURE_TILE_SIZE_X || i == A733_GPU_FW_FEATURE_TILE_SIZE_Y ||
		    i == A733_GPU_FW_FEATURE_USC_MIN_OUTPUT_REGISTERS_PER_PIX ||
		    i == A733_GPU_FW_FEATURE_NUM_CLUSTERS ||
		    i == A733_GPU_FW_FEATURE_ISP_MAX_TILES_IN_FLIGHT) {
			CHECK(error == ENOENT);
			CHECK(memcmp(&input, &saved, sizeof(input)) == 0);
		} else {
			CHECK(error == 0);
		}
	}
	return saved;
}

static struct pvr_query_profile
profiles(struct pvr_query_input input)
{
	struct pvr_query_profile profile, changed, before;
	struct pvr_query_input in;
	unsigned int i;

	CHECK(pvr_query_profile_init(&input, &profile) == 0);
	CHECK(profile.num_phantoms == 1);
	CHECK(profile.runtime.free_list_min_pages == 25);
	CHECK(profile.runtime.free_list_max_pages == 524288);
	CHECK(profile.runtime.common_store_partition_space_size == 6144);
	CHECK(profile.runtime.common_store_alloc_region_size == 11264);
	CHECK(profile.runtime.max_coeffs == 4096);
	CHECK(profile.runtime.cdm_max_local_mem_size_regs == 4096);
	CHECK(profile.quirk_count == 0 && profile.musthave_count == 0);
	CHECK(profile.enhancement_count == 1 && profile.enhancements[0] == 35421);
	memset(&before, 0xa5, sizeof(before));
	changed = before;
	CHECK(pvr_query_profile_init(NULL, &changed) == EINVAL);
	CHECK(memcmp(&changed, &before, sizeof(before)) == 0);
	CHECK(pvr_query_profile_init(&input, NULL) == EINVAL);
	for (i = 0; i < PVR_QUERY_ROGUEXE * 2 + 7; i++) {
		in = input;
		if (i < PVR_QUERY_ROGUEXE * 2)
			in.feature[i / 2] = (i & 1) != 0 ? UINT64_MAX : 0;
		else switch (i - PVR_QUERY_ROGUEXE * 2) {
		case 0: in.bvnc++; break;
		case 1: in.fw_major++; break;
		case 2: in.fw_minor++; break;
		case 3: in.fw_build++; break;
		case 4: in.brn_mask |= UINT64_C(1) << 63; break;
		case 5: in.ern_mask |= UINT64_C(1) << 63; break;
		case 6: in.feature[PVR_QUERY_ROGUEXE] = 2; break;
		}
		changed = before;
		CHECK(pvr_query_profile_init(&in, &changed) != 0);
		CHECK(memcmp(&changed, &before, sizeof(before)) == 0);
	}
	in = input;
	in.feature[PVR_QUERY_MAX_PARTITIONS] = UINT32_MAX;
	CHECK(pvr_query_profile_init(&in, &changed) == ERANGE);
	in = input;
	in.feature[PVR_QUERY_COMMON_STORE] = 7167;
	CHECK(pvr_query_profile_init(&in, &changed) == ERANGE);
	in.feature[PVR_QUERY_COMMON_STORE] = 7168;
	CHECK(pvr_query_profile_init(&in, &changed) == ERANGE); /* Coefficients underflow. */
	in = input;
	in.brn_mask &= ~UINT64_C(1);
	CHECK(pvr_query_profile_init(&in, &changed) == 0);
	CHECK(changed.runtime.common_store_alloc_region_size == 12288);
	CHECK(changed.runtime.max_coeffs == 5120);
	CHECK(changed.runtime.cdm_max_local_mem_size_regs == 4096);
	in = input;
	in.ern_mask &= ~(UINT64_C(1) << 2);
	CHECK(pvr_query_profile_init(&in, &changed) == 0);
	CHECK(changed.runtime.max_coeffs == 2048);
	CHECK(changed.runtime.cdm_max_local_mem_size_regs == 2048);
	in = input;
	in.ern_mask |= UINT64_C(1) << 1;
	CHECK(pvr_query_profile_init(&in, &changed) == 0);
	CHECK(changed.runtime.max_coeffs == 2048);
	in = input;
	in.brn_mask |= UINT64_C(1) << 2;
	in.feature[PVR_QUERY_COMPUTE_OVERLAP] = 0;
	CHECK(pvr_query_profile_init(&in, &changed) == 0);
	CHECK(changed.runtime.max_coeffs == 6144);
	CHECK(changed.runtime.cdm_max_local_mem_size_regs == 4096);
	in = input;
	in.feature[PVR_QUERY_NUM_CLUSTERS] = 5;
	CHECK(pvr_query_profile_init(&in, &changed) == 0);
	CHECK(changed.num_phantoms == 2 && changed.runtime.max_coeffs == 7168);
	in = input;
	in.feature[PVR_QUERY_TILE_X] = 32;
	in.feature[PVR_QUERY_COMMON_STORE] = 32768;
	CHECK(pvr_query_profile_init(&in, &changed) == 0);
	CHECK(changed.runtime.common_store_partition_space_size == 12288);
	in = input;
	in.brn_mask |= UINT64_C(1) << 10;
	CHECK(pvr_query_profile_init(&in, &changed) == 0);
	CHECK(changed.runtime.free_list_min_pages == 40);
	in.feature[PVR_QUERY_ROGUEXE] = 0;
	CHECK(pvr_query_profile_init(&in, &changed) == 0);
	CHECK(changed.runtime.free_list_min_pages == 50);
	in = input;
	in.brn_mask |= (1<<1) | (1<<4) | (1<<7) | (1<<3) | (1<<6);
	in.ern_mask |= 1<<3;
	CHECK(pvr_query_profile_init(&in, &changed) == 0);
	CHECK(changed.quirk_count == 5 && changed.musthave_count == 3);
	{
		const uint32_t expected[] = {47217, 49927, 62269, 48545, 51764};
		CHECK(memcmp(changed.quirks, expected, sizeof(expected)) == 0);
	}
	CHECK(changed.enhancement_count == 2 && changed.enhancements[1] == 42064);
	return profile;
}

static struct drm_pvr_ioctl_dev_query_args
prepare(uint32_t type, uint32_t size, uint64_t array, uint16_t count)
{
	struct drm_pvr_dev_query_quirks q = {array, count, 0, 0};
	struct drm_pvr_ioctl_dev_query_args args = {type, size, (uintptr_t)(arena + QUERY)};

	memset(arena, 0xa5, sizeof(arena));
	if (type != DRM_PVR_DEV_QUERY_RUNTIME_INFO_GET) {
		memset(arena + QUERY, 0, 1024);
		memcpy(arena + QUERY, &q, sizeof(q));
	}
	copies = fail_copy = 0;
	return args;
}

static void
queries(const struct pvr_query_profile *p)
{
	struct drm_pvr_ioctl_dev_query_args args, saved;
	struct drm_pvr_dev_query_quirks q;
	uint8_t before[sizeof(arena)];
	uint32_t type, size, expected;
	unsigned int i, fail, total;

	for (type = 1; type <= 3; type++) {
		expected = type == 1 ? 32 : 16;
		args = (struct drm_pvr_ioctl_dev_query_args){type, UINT32_MAX, 0};
		copies = 0;
		CHECK(pvr_query(p, type, &args) == 0);
		CHECK(args.size == expected && copies == 0);
		for (size = 0; size <= 600; size++) {
			args = prepare(type, size, 0, 0);
			saved = args;
			memcpy(before, arena, sizeof(arena));
			if (size < expected) {
				CHECK(pvr_query(p, type, &args) == -EINVAL);
				CHECK(copies == 0 && memcmp(&args, &saved, sizeof(args)) == 0);
				CHECK(memcmp(before, arena, sizeof(arena)) == 0);
				continue;
			}
			CHECK(pvr_query(p, type, &args) == 0 && args.size == expected);
			if (type == 1)
				CHECK(memcmp(arena + QUERY, &p->runtime, expected) == 0);
			else {
				memcpy(&q, arena + QUERY, sizeof(q));
				CHECK(q.count == (type == 2 ? 0 : 1));
			}
			for (i = expected; i < size; i++)
				CHECK(arena[QUERY + i] == 0);
			CHECK(memcmp(arena, before, QUERY) == 0);
			CHECK(memcmp(arena + QUERY + size, before + QUERY + size,
			    sizeof(arena) - QUERY - size) == 0);
		}
		args = prepare(type, 600, (uintptr_t)(arena + ARRAY), UINT16_MAX);
		CHECK(pvr_query(p, type, &args) == 0);
		total = copies;
		for (fail = 1; fail <= total; fail++) {
			args = prepare(type, 600, (uintptr_t)(arena + ARRAY), UINT16_MAX);
			saved = args;
			fail_copy = fail;
			CHECK(pvr_query(p, type, &args) == -EFAULT);
			CHECK(copies == fail && memcmp(&args, &saved, sizeof(args)) == 0);
		}
		args = prepare(type, expected, 0, 0);
		args.pointer = UINT64_MAX - 2;
		saved = args;
		CHECK(pvr_query(p, type, &args) == -EFAULT && copies == 0);
		CHECK(memcmp(&args, &saved, sizeof(args)) == 0);
		args.pointer = 1;
		CHECK(pvr_query(p, type, &args) == -EFAULT);
	}
	for (type = 2; type <= 3; type++) {
		for (i = 0; i < 4; i++) {
			uint16_t capacity = i == 3 ? UINT16_MAX : i;
			args = prepare(type, 16, (uintptr_t)(arena + ARRAY), capacity);
			saved = args;
			memcpy(before, arena, sizeof(arena));
			if (type == 3 && capacity == 0) {
				CHECK(pvr_query(p, type, &args) == -E2BIG);
				CHECK(memcmp(before, arena, sizeof(arena)) == 0);
				CHECK(memcmp(&args, &saved, sizeof(args)) == 0);
			} else {
				uint32_t value;
				CHECK(pvr_query(p, type, &args) == 0);
				memcpy(&q, arena + QUERY, sizeof(q));
				CHECK(q.count == (type == 2 ? 0 : 1));
				memcpy(&value, arena + ARRAY, sizeof(value));
				CHECK(value == (type == 2 ? UINT32_C(0xa5a5a5a5) : 35421));
				CHECK(arena[ARRAY + 4] == 0xa5);
			}
		}
		args = prepare(type, 600, 0, 0);
		arena[QUERY + 511] = 1;
		CHECK(pvr_query(p, type, &args) == -E2BIG && args.size == 600);
		args = prepare(type, 16, 0, 0);
		arena[QUERY + 12] = 1;
		CHECK(pvr_query(p, type, &args) == -EINVAL);
	}
	args = prepare(3, 16, 0, 0);
	arena[QUERY + 10] = 1;
	CHECK(pvr_query(p, 3, &args) == -EINVAL);
	args = prepare(2, 16, 0, 0);
	arena[QUERY + 10] = 7;
	CHECK(pvr_query(p, 2, &args) == 0 && arena[QUERY + 10] == 7);
	args = prepare(2, 16, UINT64_MAX, 0);
	CHECK(pvr_query(p, 2, &args) == 0); /* Empty data does not dereference the pointer. */
	CHECK(arena[QUERY + 10] == 0);
	args = prepare(3, 16, UINT64_MAX - 1, 1);
	CHECK(pvr_query(p, 3, &args) == -EFAULT);
	args = prepare(6, 16, 0, 0);
	CHECK(pvr_query(p, 6, &args) == -EOPNOTSUPP && copies == 0);
}

int
main(int argc, char **argv)
{
	struct pvr_query_profile profile;

	CHECK(argc == 2);
	profile = profiles(metadata(argv[1]));
	queries(&profile);
	printf("PASS: %u metadata-derived PowerVR query checks\n", checks);
	return 0;
}
