/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD finite native TFU raster acceptance, 2026-10-21. */
#define main tfu_acceptance_main
#include "bcm2712-v3d-render-check.c"
#undef main
#include <time.h>
#include "mesa-tfu-oracle.h"

#define TFU_BO_SIZE (24U * 1024)
#define TFU_ROUNDS 4U
/* Mesa26.2.4 v3d_tfu.h and v3d_packet.xml: raster, unfiltered R32F. */
#define TFU_R32F (29U << 16)
#define TFU_IMAGE_BASE 4096U
#define TFU_IMAGE_BYTES (64U * 64U * 4U)
struct raster_case {
	unsigned int width, height, stride, source, destination;
};
static const struct raster_case raster_cases[] = {
	{64, 64, 64, 0, 0},
	{84, 52, 96, 256, 512}
};

static uint32_t
raster_guard(size_t word)
{

	return GUARD ^ (uint32_t)word * UINT32_C(0x01020305);
}

static void
raster_fence(int fd, uint32_t sync_handle)
{
	struct timespec now;
	struct drm_syncobj_wait wait_args = {
	    .handles = (uintptr_t)&sync_handle, .count_handles = 1,
	    .flags = DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL};

	check(clock_gettime(CLOCK_MONOTONIC, &now) == 0, "TFU clock_gettime");
	check(now.tv_sec >= 0 && now.tv_nsec >= 0 && now.tv_nsec < 1000000000 &&
	    (uint64_t)now.tv_sec < (uint64_t)INT64_MAX / 1000000000 - 1,
	    "TFU invalid monotonic deadline");
	wait_args.timeout_nsec = (int64_t)now.tv_sec * 1000000000 + now.tv_nsec +
	    1000000000;
	if (ioctl(fd, DRM_IOCTL_SYNCOBJ_WAIT, &wait_args) < 0)
		err(1, "TFU syncobj wait");
}

static void
tfu_complete(int fd, const struct buffer *src, const struct buffer *dst,
    uint32_t sync_handle)
{

	raster_fence(fd, sync_handle);
	for (unsigned int b = 0; b < 2; b++) {
		struct drm_v3d_wait_bo wait_args = {
		    .handle = b == 0 ? src->handle : dst->handle,
		    .timeout_ns = UINT64_C(1000000000)};
		if (ioctl(fd, DRM_IOCTL_V3D_WAIT_BO, &wait_args) < 0)
			err(1, "TFU WAIT_BO");
	}
}

static void
raster_test(int fd, const struct buffer *src, const struct buffer *dst,
    uint32_t sync_handle, const struct raster_case *c, unsigned int ioc,
    unsigned int *jobs)
{
	const size_t words = TFU_BO_SIZE / sizeof(uint32_t);
	const size_t source = c->source / sizeof(uint32_t);
	const size_t destination = c->destination / sizeof(uint32_t);
	const uint64_t span = (uint64_t)c->stride * c->height;
	struct drm_v3d_submit_tfu tfu = {
	    .icfg = TFU_R32F | ioc, .iia = src->gpu + c->source,
	    .iis = c->stride, .ioa = dst->gpu + c->destination,
	    .ios = (c->height << 16) | c->width,
	    .bo_handles = {dst->handle, src->handle},
	    .in_sync = sync_handle, .out_sync = sync_handle,
	    .v71 = {.ioc = c->stride << 16}};

	check(c->width != 0 && c->height != 0 && c->width <= c->stride &&
	    c->stride <= UINT16_MAX && c->height <= UINT16_MAX && ioc <= 1 &&
	    c->source % 4 == 0 && c->destination % 4 == 0 &&
	    source + span <= words && destination + span <= words,
	    "TFU raster case bounds");
	for (unsigned int round = 0; round < TFU_ROUNDS; round++) {
		unsigned int iteration = (*jobs)++;

		for (size_t word = 0; word < words; word++) {
			src->map[word] = pattern(iteration, word);
			dst->map[word] = raster_guard(word);
		}
		if (ioctl(fd, DRM_IOCTL_V3D_SUBMIT_TFU, &tfu) < 0)
			err(1, "SUBMIT_TFU %ux%u stride%u IOC%u round%u",
			    c->width, c->height, c->stride, ioc, round);
		tfu_complete(fd, src, dst, sync_handle);
		for (size_t word = 0; word < words; word++) {
			uint32_t expected = raster_guard(word);

			check(src->map[word] == pattern(iteration, word),
			    "TFU modified source");
			if (word >= destination && word - destination < span) {
				size_t offset = word - destination;
				/* Do not use intra-row padding as an oracle. */
				if (offset % c->stride >= c->width)
					continue;
				expected = pattern(iteration, source + offset);
			}
			if (dst->map[word] != expected)
				errx(1, "TFU copy/guard job%u word%zu: %#x expected %#x",
				    iteration, word, dst->map[word], expected);
		}
	}
	printf("PASS: TFU %ux%u stride%u offsets%u/%u IOC%u: %u jobs\n",
	    c->width, c->height, c->stride, c->source, c->destination,
	    ioc, TFU_ROUNDS);
}

static void
tiled_test(int fd, const struct buffer *src, const struct buffer *dst,
    uint32_t sync_handle, unsigned int *jobs)
{
	uint32_t *reference = malloc(2 * TFU_BO_SIZE);
	uint32_t *expected, *source_snapshot;
	const uint32_t uif_stride = 64 / (2 * v3d_utile_height(4));
	bool visited[64 * 64] = {false};

	if (reference == NULL)
		errx(1, "TFU tiled oracle allocation");
	expected = reference;
	source_snapshot = reference + TFU_BO_SIZE / sizeof(uint32_t);
	/* This shape has no tile padding. Prove the oracle covers it once. */
	for (unsigned int y = 0; y < 64; y++) {
		for (unsigned int x = 0; x < 64; x++) {
			uint32_t offset =
			    v3d_get_uif_no_xor_pixel_offset(4, 64, x, y);
			check(offset % 4 == 0 && offset < TFU_IMAGE_BYTES &&
			    !visited[offset / 4], "TFU tiled oracle bounds/alias");
			visited[offset / 4] = true;
		}
	}
	for (unsigned int direction = 0; direction < 2; direction++) {
		struct drm_v3d_submit_tfu tfu = {
		    .icfg = TFU_R32F | (direction ? V3D71_TFU_ICFG_FORMAT_UIF_NO_XOR <<
		        V3D71_TFU_ICFG_IFORMAT_SHIFT : 0),
		    .iia = src->gpu + TFU_IMAGE_BASE,
		    .iis = direction ? uif_stride : 64,
		    .ioa = dst->gpu + TFU_IMAGE_BASE,
		    .ios = (64U << 16) | 64U,
		    .bo_handles = {dst->handle, src->handle},
		    .in_sync = sync_handle, .out_sync = sync_handle,
		    .v71 = {.ioc = direction ? 64U << V3D71_TFU_IOC_STRIDE_SHIFT :
		        (uif_stride << V3D71_TFU_IOC_STRIDE_SHIFT) |
		        (V3D71_TFU_IOC_FORMAT_UIF_NO_XOR <<
		        V3D71_TFU_IOC_FORMAT_SHIFT)}};

		for (unsigned int round = 0; round < TFU_ROUNDS; round++) {
			unsigned int iteration = (*jobs)++;

			for (size_t word = 0; word < TFU_BO_SIZE / 4; word++) {
				src->map[word] = raster_guard(word) ^ iteration;
				dst->map[word] = expected[word] = raster_guard(word);
			}
			/* Refill the input independently before every direction/job. */
			for (unsigned int y = 0; y < 64; y++) {
				for (unsigned int x = 0; x < 64; x++) {
					uint32_t linear = (y * 64 + x) * 4;
					uint32_t tile =
					    v3d_get_uif_no_xor_pixel_offset(4, 64, x, y);
					uint32_t input = TFU_IMAGE_BASE +
					    (direction ? tile : linear);
					uint32_t output = TFU_IMAGE_BASE +
					    (direction ? linear : tile);
					uint32_t value = pattern(iteration, y * 64 + x);

					src->map[input / 4] = value;
					expected[output / 4] = value;
				}
			}
			memcpy(source_snapshot, src->map, TFU_BO_SIZE);
			if (ioctl(fd, DRM_IOCTL_V3D_SUBMIT_TFU, &tfu) < 0)
				err(1, "SUBMIT_TFU tiled direction%u round%u",
				    direction, round);
			tfu_complete(fd, src, dst, sync_handle);
			for (size_t word = 0; word < TFU_BO_SIZE / 4; word++) {
				check(src->map[word] == source_snapshot[word],
				    "TFU tiled modified source");
				if (dst->map[word] != expected[word])
					errx(1, "TFU tiled copy/guard job%u word%zu: %#x expected %#x",
					    iteration, word, dst->map[word], expected[word]);
			}
		}
		printf("PASS: TFU %s: %u independent jobs, tiled bytes and outer guards\n",
		    direction ? "UIF_NO_XOR-to-raster" : "raster-to-UIF_NO_XOR", TFU_ROUNDS);
	}
	free(reference);
}

int
main(int argc, char **argv)
{
	struct drm_syncobj_create sync_create = {.flags = DRM_SYNCOBJ_CREATE_SIGNALED};
	struct buffer src, dst;
	unsigned int jobs = 0;
	int fd;

	if (argc != 2)
		errx(1, "usage: %s /dev/dri/renderD128", argv[0]);
	setvbuf(stdout, NULL, _IONBF, 0);
	fd = open(argv[1], O_RDWR | O_CLOEXEC);
	if (fd < 0) err(1, "open TFU node");
	src = allocate_bytes(fd, TFU_BO_SIZE);
	dst = allocate_bytes(fd, TFU_BO_SIZE);
	check(src.handle != dst.handle &&
	    (uint64_t)src.gpu + TFU_BO_SIZE <= UINT64_C(0x100000000) &&
	    (uint64_t)dst.gpu + TFU_BO_SIZE <= UINT64_C(0x100000000) &&
	    ((uint64_t)src.gpu + TFU_BO_SIZE <= dst.gpu ||
	    (uint64_t)dst.gpu + TFU_BO_SIZE <= src.gpu), "TFU overlapping BOs");
	if (ioctl(fd, DRM_IOCTL_SYNCOBJ_CREATE, &sync_create) < 0)
		err(1, "TFU SYNCOBJ_CREATE");
	for (size_t c = 0; c < sizeof(raster_cases) / sizeof(raster_cases[0]); c++) {
		raster_test(fd, &src, &dst, sync_create.handle, &raster_cases[c], 1, &jobs);
		raster_test(fd, &src, &dst, sync_create.handle, &raster_cases[c], 0, &jobs);
	}
	tiled_test(fd, &src, &dst, sync_create.handle, &jobs);
	close_handle(fd, src.handle);
	close_handle(fd, dst.handle);
	check(munmap(src.map, TFU_BO_SIZE) == 0, "TFU source munmap");
	check(munmap(dst.map, TFU_BO_SIZE) == 0, "TFU destination munmap");
	check(close(fd) == 0, "TFU close");
	check(jobs == 24, "TFU job count");
	puts("PASS: 24 TFU copies, 100480 pixels, independent tiled bytes, "
	    "source/outer guards and binary fences");
	return 0;
}
