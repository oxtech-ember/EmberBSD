/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Actual native allocation and dumb callback, with controlled kernel services. */
#include "../../sys/external/bsd/drm2/bcmv3d/bcmv3d_dumb.h"
#define BCMV3D_DUMB_CONTRACT
#define main bcmv3d_bo_fixture_main
#include "bcm2712-v3d-bo-contract.c"
#undef main

struct dumb_geometry {
	uint32_t width, height, pitch;
	uint64_t size;
};

static void
check_success(const struct dumb_geometry *geometry)
{
	struct bcmv3d_device native = { .bo_total = BCMV3D_BO_TOTAL_MAX };
	struct drm_device dev = { .dev_private = &native };
	struct drm_file file = { 0 };
	struct drm_mode_create_dumb args = {
		.width = geometry->width, .height = geometry->height, .bpp = 32,
		.handle = UINT32_MAX, .pitch = UINT32_MAX, .size = UINT64_MAX
	};
	struct drm_gem_object *object;
	struct bcmv3d_bo *bo;

	native.drm = &dev;
	CHECK(bcmv3d_dumb_create(&file, &dev, &args) == 0);
	CHECK(args.width == geometry->width && args.height == geometry->height);
	CHECK(args.bpp == 32 && args.flags == 0);
	CHECK(args.handle != 0 && args.handle < 16);
	CHECK(args.pitch == geometry->pitch && args.size == geometry->size);
	object = file.handles[args.handle];
	CHECK(object != NULL && object->refs == 1);
	CHECK(object->size == geometry->size && native.allocated == geometry->size);
	CHECK(bcmv3d_bo_reference(&native, object, &bo) == 0);
	CHECK(object->refs == 2 && bo->cma.dmasize == geometry->size);
	CHECK(((const unsigned char *)bo->cma.vaddr)[0] == 0);
	CHECK(((const unsigned char *)bo->cma.vaddr)[geometry->size - 1] == 0);
	drm_gem_object_put_unlocked(object);
	CHECK(object->refs == 1);
	close_handle(&file, args.handle);
	empty(&native);
	CHECK(native.bo_stats.reservations == 1 && native.bo_stats.releases == 1);
	CHECK(native.bo_stats.failures == 0);
}

static void
check_refusal(uint32_t width, uint32_t height, uint32_t bpp, uint32_t flags)
{
	struct bcmv3d_device native = { .bo_total = BCMV3D_BO_TOTAL };
	struct drm_device dev = { .dev_private = &native };
	struct drm_file file = { 0 };
	struct drm_mode_create_dumb args = {
		.width = width, .height = height, .bpp = bpp, .flags = flags,
		.handle = UINT32_MAX, .pitch = UINT32_MAX, .size = UINT64_MAX
	};

	native.drm = &dev;
	CHECK(bcmv3d_dumb_create(&file, &dev, &args) == -EINVAL);
	CHECK(args.handle == 0 && args.pitch == 0 && args.size == 0);
	CHECK(args.width == width && args.height == height);
	CHECK(args.bpp == bpp && args.flags == flags);
	CHECK(native.bo_stats.reservations == 0 && native.bo_stats.failures == 0);
	empty(&native);
}

static void
check_failure(unsigned int failure)
{
	struct bcmv3d_device native = { .bo_total = BCMV3D_BO_TOTAL };
	struct drm_device dev = { .dev_private = &native };
	struct drm_file file = { 0 };
	struct drm_mode_create_dumb args = {
		.width = 17, .height = 33, .bpp = 32,
		.handle = UINT32_MAX, .pitch = UINT32_MAX, .size = UINT64_MAX
	};

	int expected = failure == 5 ? EIO : ENOMEM;

	native.drm = &dev;
	fail = failure;
	CHECK(bcmv3d_dumb_create(&file, &dev, &args) == -expected);
	fail = 0;
	CHECK(args.handle == 0 && args.pitch == 0 && args.size == 0);
	CHECK(native.bo_stats.failures == 1);
	CHECK(native.bo_stats.last_errno == (unsigned int)expected);
	if (failure == 6)
		CHECK(native.bo_stats.last_stage == BCMV3D_BO_STAGE_HANDLE);
	empty(&native);
}

static void
check_full_handle_table(void)
{
	struct bcmv3d_device native = { .bo_total = BCMV3D_BO_TOTAL };
	struct drm_device dev = { .dev_private = &native };
	struct drm_file file = { 0 };
	struct drm_mode_create_dumb args;
	unsigned int i;

	native.drm = &dev;
	for (i = 1; i < 16; i++) {
		args = (struct drm_mode_create_dumb) {
			.width = 1, .height = 1, .bpp = 32
		};
		CHECK(bcmv3d_dumb_create(&file, &dev, &args) == 0);
		CHECK(args.handle == i && file.handles[i]->refs == 1);
	}
	args = (struct drm_mode_create_dumb) {
		.width = 1, .height = 1, .bpp = 32,
		.handle = UINT32_MAX, .pitch = UINT32_MAX, .size = UINT64_MAX
	};
	CHECK(bcmv3d_dumb_create(&file, &dev, &args) == -ENOSPC);
	CHECK(args.handle == 0 && args.pitch == 0 && args.size == 0);
	CHECK(native.bo_stats.last_errno == ENOSPC);
	CHECK(native.bo_stats.last_stage == BCMV3D_BO_STAGE_HANDLE);
	for (i = 1; i < 16; i++)
		close_handle(&file, i);
	empty(&native);
	CHECK(native.bo_stats.reservations == 16 && native.bo_stats.releases == 16);
}

int
main(void)
{
	const struct dumb_geometry geometry[] = {
		{ 1, 1, 64, PAGE_SIZE },
		{ 15, 1, 64, PAGE_SIZE },
		{ 16, 1, 64, PAGE_SIZE },
		{ 17, 1, 128, PAGE_SIZE },
		{ 31, 1, 128, PAGE_SIZE },
		{ 32, 1, 128, PAGE_SIZE },
		{ 33, 1, 192, PAGE_SIZE },
		{ 17, 33, 128, PAGE_SIZE == 4096 ? 8192 : 65536 },
		{ 1920, 1080, 7680, PAGE_SIZE == 4096 ? 8294400 : 8323072 },
		{ 4096, 1024, 16384, 16777216 },
		{ 4194304, 1, 16777216, 16777216 },
		{ 1, 262144, 64, 16777216 },
		{ 4096, 1025, 16384, PAGE_SIZE == 4096 ? 16793600 : 16842752 },
		{ 67108864, 1, 268435456, 268435456 }
	};
	const uint32_t invalid_bpp[] = { 0, 1, 8, 16, 24, 64, UINT32_MAX };
	unsigned int i;

	CHECK(PAGE_SIZE == 4096 || PAGE_SIZE == 65536);
	for (i = 0; i < sizeof(geometry) / sizeof(geometry[0]); i++)
		check_success(&geometry[i]);
	/* A scattered native allocation must not require one contiguous segment. */
	fragment_pages = 1;
	contiguous_limit = PAGE_SIZE;
	check_success(&geometry[8]);
	fragment_pages = 0;
	contiguous_limit = 0;
	check_refusal(0, 1, 32, 0);
	check_refusal(1, 0, 32, 0);
	check_refusal(UINT32_MAX, UINT32_MAX, 32, 0);
	check_refusal(UINT32_MAX, 1, 32, 0);
	check_refusal(UINT32_C(0x40000000), UINT32_C(0x40000000), 32, 0);
	check_refusal(67108865, 1, 32, 0);
	check_refusal(1, 4194305, 32, 0);
	check_refusal(4096, 16385, 32, 0);
	for (i = 0; i < sizeof(invalid_bpp) / sizeof(invalid_bpp[0]); i++)
		check_refusal(1, 1, invalid_bpp[i], 0);
	check_refusal(1, 1, 32, 1);
	check_refusal(1, 1, 32, UINT32_MAX);
	for (i = 1; i <= 6; i++)
		check_failure(i);
	check_full_handle_table();
	printf("PASS actual native dumb callback page%u: geometry, overflow, "
	    "allocation/handle errors and reference balance (%u checks)\n",
	    (unsigned int)PAGE_SIZE, checks);
	return 0;
}
