/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual userspace TFU checker with controlled host DRM services. */
#include <sys/types.h>
#include <sys/mman.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#if !defined(__NetBSD__) && !defined(__linux__)
typedef int8_t __s8;
typedef uint8_t __u8;
typedef int16_t __s16;
typedef uint16_t __u16;
typedef int32_t __s32;
typedef uint32_t __u32;
typedef int64_t __s64;
typedef uint64_t __u64;
typedef size_t __kernel_size_t;
typedef unsigned long drm_handle_t;
#define __user
#endif
#include "model-mesa-tfu-oracle.h"

static void *model_malloc(size_t);
static int model_open(const char *, int, ...);
static int model_close(int);
static int model_ioctl(int, unsigned long, ...);
static void *model_mmap(const void *, size_t, int, int, int, off_t);
static int model_munmap(const void *, size_t);
static int model_clock_gettime(clockid_t, struct timespec *);
#define malloc model_malloc
#define open model_open
#define close model_close
#define ioctl model_ioctl
#define mmap model_mmap
#define munmap model_munmap
#define clock_gettime model_clock_gettime
#include "checker-body.h"
#undef malloc
#undef open
#undef close
#undef ioctl
#undef mmap
#undef munmap
#undef clock_gettime
#undef main

#define CHECK(c) do { if (!(c)) { \
	fprintf(stderr, "FAIL contract:%u: %s\n", (unsigned int)__LINE__, #c); exit(1); \
} } while (0)
struct model_bo {
	uint32_t *data;
	uint32_t handle, gpu, size;
	bool handle_live, map_live;
};
static struct model_bo bos[2];
static unsigned int created, closed, mapped, unmapped, submitted;
static unsigned int fence_waits, bo_waits, fd_live, sync_live, live_bytes, peak;
static const char *mode;

static void
process_release(void)
{

	/* Model process teardown after expected err(1) paths, not kernel refs. */
	for (unsigned int i = 0; i < 2; i++)
		free(bos[i].data);
}

static void *
model_malloc(size_t length)
{

	CHECK(length == 2 * TFU_BO_SIZE);
	if (strcmp(mode, "oracle-memory") == 0) return NULL;
	return malloc(length);
}

static struct model_bo *
find(uint32_t handle)
{

	CHECK(handle >= 1 && handle <= created && handle <= 2);
	CHECK(bos[handle - 1].handle_live);
	return &bos[handle - 1];
}

static int
model_open(const char *path, int flags, ...)
{

	CHECK(strcmp(path, "model-v3d") == 0 && flags == (O_RDWR | O_CLOEXEC));
	CHECK(!fd_live);
	if (strcmp(mode, "open") == 0) { errno = EIO; return -1; }
	fd_live = 1;
	return 77;
}

static int
model_close(int fd)
{

	CHECK(fd == 77 && fd_live && closed == created && unmapped == mapped);
	CHECK(live_bytes == 0);
	if (strcmp(mode, "fd-close") == 0) { errno = EIO; return -1; }
	fd_live = sync_live = 0;
	return 0;
}

static void *
model_mmap(const void *address, size_t length, int protection, int flags, int fd,
    off_t offset)
{
	struct model_bo *bo = find((uint32_t)(offset / 65536));

	CHECK(address == NULL && fd == 77 && fd_live);
	CHECK(protection == (PROT_READ | PROT_WRITE) && flags == MAP_SHARED);
	CHECK(length == bo->size && !bo->map_live);
	if (strcmp(mode, "map") == 0) { errno = ENOMEM; return MAP_FAILED; }
	bo->map_live = true; mapped++;
	if (strcmp(mode, "zero") == 0) bo->data[length / 4 - 1] = 1;
	return bo->data;
}

static int
model_munmap(const void *address, size_t length)
{

	if (strcmp(mode, "unmap") == 0) { errno = EIO; return -1; }
	for (unsigned int i = 0; i < created; i++) {
		struct model_bo *bo = &bos[i];
		if (bo->data != address) continue;
		CHECK(length == bo->size && bo->map_live && !bo->handle_live);
		bo->map_live = false; unmapped++; live_bytes -= bo->size;
		free(bo->data); bo->data = NULL;
		return 0;
	}
	CHECK(false);
	return -1;
}

static int
model_clock_gettime(clockid_t clock_id, struct timespec *now)
{

	CHECK(clock_id == CLOCK_MONOTONIC);
	if (strcmp(mode, "clock") == 0) { errno = EIO; return -1; }
	now->tv_sec = 123; now->tv_nsec = 456000000;
	return 0;
}

static int
submit_tiled(const struct drm_v3d_submit_tfu *a)
{
	unsigned int direction = (submitted - 16) / 4;
	struct model_bo *src = find(a->bo_handles[1]), *dst = find(a->bo_handles[0]);

	CHECK(submitted >= 16 && submitted < 24 && direction < 2);
	CHECK(src == &bos[0] && dst == &bos[1] && src->map_live && dst->map_live);
	CHECK(a->icfg == ((29U << 16) | (direction ? 6U << 23 : 0)));
	CHECK(a->iia == src->gpu + 4096 && a->ioa == dst->gpu + 4096);
	CHECK(a->iis == (direction ? 8U : 64U) && a->ios == 0x00400040);
	CHECK(a->v71.ioc == (direction ? 64U << 16 : (8U << 16) | (6U << 12)));
	CHECK(a->bo_handles[2] == 0 && a->bo_handles[3] == 0);
	CHECK(a->in_sync == 9 && a->out_sync == 9 && sync_live);
	CHECK(a->flags == 0 && a->extensions == 0 && a->v71.pad == 0);
	CHECK(a->ica == 0 && a->iua == 0 && a->coef[0] == 0 &&
	    a->coef[1] == 0 && a->coef[2] == 0 && a->coef[3] == 0);
	CHECK(fence_waits == submitted && bo_waits == submitted * 2);
	for (unsigned int y = 0; y < 64; y++) {
		for (unsigned int x = 0; x < 64; x++) {
			uint32_t tile = model_v3d_get_uif_no_xor_pixel_offset(4, 64, x, y);
			uint32_t linear = (y * 64 + x) * 4;
			uint32_t from = (4096 + (direction ? tile : linear)) / 4;
			uint32_t to = (4096 + (direction ? linear : tile)) / 4;

			CHECK(src->data[from] == pattern(submitted, y * 64 + x));
			if (strcmp(mode, "tiled-no-copy") != 0 &&
			    !(strcmp(mode, "reverse-no-copy") == 0 && direction))
				dst->data[to] = src->data[from];
		}
	}
	if (strcmp(mode, "tiled-memcpy") == 0 ||
	    (strcmp(mode, "forward-memcpy") == 0 && direction == 0))
		memcpy(dst->data + 1024, src->data + 1024, 16384);
	if (strcmp(mode, "tiled-source") == 0) src->data[src->size / 4 - 1] ^= 1;
	if (strcmp(mode, "tiled-guard") == 0) dst->data[dst->size / 4 - 1] ^= 1;
	submitted++;
	return 0;
}

static int
submit(const struct drm_v3d_submit_tfu *a)
{
	if (submitted >= 16) return submit_tiled(a);
	const bool large = submitted >= 8;
	const unsigned int width = large ? 84 : 64, height = large ? 52 : 64;
	const unsigned int stride = large ? 96 : 64;
	const unsigned int source = large ? 256 : 0, destination = large ? 512 : 0;
	const unsigned int ioc = (submitted / 4) % 2 == 0 ? 1 : 0;
	struct model_bo *src = find(a->bo_handles[1]), *dst = find(a->bo_handles[0]);

	CHECK(submitted < 16 && a->icfg == ((29U << 16) | ioc));
	CHECK(a->iia == src->gpu + source && a->ioa == dst->gpu + destination);
	CHECK(a->iis == stride && a->v71.ioc == stride << 16);
	CHECK(a->ios == (height << 16 | width));
	CHECK(src == &bos[0] && dst == &bos[1] && src->map_live && dst->map_live);
	CHECK(a->bo_handles[2] == 0 && a->bo_handles[3] == 0);
	CHECK(a->in_sync == 9 && a->out_sync == 9 && sync_live);
	CHECK(a->flags == 0 && a->extensions == 0 && a->v71.pad == 0);
	CHECK(a->ica == 0 && a->iua == 0 && a->coef[0] == 0 &&
	    a->coef[1] == 0 && a->coef[2] == 0 && a->coef[3] == 0);
	CHECK(fence_waits == submitted && bo_waits == submitted * 2);
	if (strcmp(mode, "old-ioc") == 0 && ioc == 0) { errno = EINVAL; return -1; }
	if (strcmp(mode, "submit") == 0) { errno = EIO; return -1; }
	for (unsigned int y = 0; y < height; y++) {
		for (unsigned int x = 0; x < width; x++) {
			size_t s = source / 4 + y * stride + x;
			size_t d = destination / 4 + y * stride + x;
			CHECK((s + 1) * 4 <= src->size && (d + 1) * 4 <= dst->size);
			if (strcmp(mode, "no-copy") != 0 &&
			    (strcmp(mode, "short-map") != 0 || (s < 4096 && d < 4096)))
				dst->data[d] = src->data[s];
		}
		/* A valid driver may write intra-row padding. This must not fail. */
		if (strcmp(mode, "padding") == 0)
			for (unsigned int x = width; x < stride; x++)
				dst->data[destination / 4 + y * stride + x] = 0xdeadbeef;
	}
	if (strcmp(mode, "source") == 0) src->data[src->size / 4 - 1] ^= 1;
	if (strcmp(mode, "pixel") == 0) dst->data[destination / 4] ^= 1;
	if (strcmp(mode, "guard") == 0) dst->data[dst->size / 4 - 1] ^= 1;
	submitted++;
	return 0;
}

static int
model_ioctl(int fd, unsigned long request, ...)
{
	va_list ap;
	void *arg;

	CHECK(fd == 77 && fd_live);
	va_start(ap, request); arg = va_arg(ap, void *); va_end(ap);
	if (request == DRM_IOCTL_V3D_CREATE_BO) {
		struct drm_v3d_create_bo *a = arg;
		uint32_t size = strcmp(mode, "legacy") == 0 ? BO_SIZE : TFU_BO_SIZE;
		CHECK(created < 2 && a->size == size && a->flags == 0);
		if ((strcmp(mode, "create1") == 0 && created == 0) ||
		    (strcmp(mode, "create2") == 0 && created == 1)) {
			errno = ENOMEM; return -1;
		}
		struct model_bo *bo = &bos[created++];
		bo->handle = created; bo->gpu = created * 65536; bo->size = size;
		bo->data = calloc(1, size); CHECK(bo->data != NULL);
		bo->handle_live = true; a->handle = bo->handle; a->offset = bo->gpu;
		live_bytes += size; if (live_bytes > peak) peak = live_bytes;
		return 0;
	}
	if (request == DRM_IOCTL_V3D_MMAP_BO) {
		struct drm_v3d_mmap_bo *a = arg;
		(void)find(a->handle);
		if (strcmp(mode, "mmap-ioctl") == 0) { errno = EIO; return -1; }
		a->offset = a->handle * 65536;
		return 0;
	}
	if (request == DRM_IOCTL_V3D_GET_BO_OFFSET) {
		struct drm_v3d_get_bo_offset *a = arg;
		a->offset = find(a->handle)->gpu;
		if (strcmp(mode, "get-offset") == 0) { errno = EIO; return -1; }
		if (strcmp(mode, "wrong-offset") == 0) a->offset++;
		return 0;
	}
	if (request == DRM_IOCTL_SYNCOBJ_CREATE) {
		struct drm_syncobj_create *a = arg;
		CHECK(a->flags == DRM_SYNCOBJ_CREATE_SIGNALED && !sync_live);
		if (strcmp(mode, "sync-create") == 0) { errno = EIO; return -1; }
		a->handle = 9; sync_live = 1;
		return 0;
	}
	if (request == DRM_IOCTL_V3D_SUBMIT_TFU) return submit(arg);
	if (request == DRM_IOCTL_SYNCOBJ_WAIT) {
		const struct drm_syncobj_wait *a = arg;
		CHECK(a->timeout_nsec == INT64_C(124456000000));
		CHECK(a->count_handles == 1 && a->flags == DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL);
		CHECK(*(uint32_t *)(uintptr_t)a->handles == 9 && fence_waits + 1 == submitted);
		if (strcmp(mode, "sync-wait") == 0) { errno = ETIMEDOUT; return -1; }
		fence_waits++;
		return 0;
	}
	if (request == DRM_IOCTL_V3D_WAIT_BO) {
		const struct drm_v3d_wait_bo *a = arg;
		CHECK(a->handle == bo_waits % 2 + 1 && a->timeout_ns == UINT64_C(1000000000));
		CHECK(fence_waits == submitted && bo_waits < submitted * 2);
		if (strcmp(mode, "bo-wait") == 0) { errno = EIO; return -1; }
		bo_waits++;
		return 0;
	}
	if (request == DRM_IOCTL_GEM_CLOSE) {
		const struct drm_gem_close *a = arg;
		struct model_bo *bo = find(a->handle);
		CHECK(bo->handle_live && bo_waits == submitted * 2 && fence_waits == submitted);
		if (strcmp(mode, "gem-close") == 0) { errno = EIO; return -1; }
		bo->handle_live = false; closed++;
		return 0;
	}
	CHECK(false);
	return -1;
}

int
main(int argc, char **argv)
{
	char *args[] = {"tfu-check", "model-v3d", NULL};

	CHECK(argc == 2); mode = argv[1];
	CHECK(atexit(process_release) == 0);
	if (strcmp(mode, "legacy") == 0) {
		int fd = model_open("model-v3d", O_RDWR | O_CLOEXEC);
		struct buffer bo = allocate(fd);
		close_handle(fd, bo.handle);
		CHECK(model_munmap(bo.map, BO_SIZE) == 0 && model_close(fd) == 0);
		CHECK(created == 1 && closed == 1 && peak == BO_SIZE);
		puts("PASS: legacy20KiB allocation and release"); return 0;
	}
	CHECK(tfu_check_main(2, args) == 0);
	if (strcmp(mode, "pass") != 0 && strcmp(mode, "padding") != 0) {
		fputs("FAIL: injected failure was not detected\n", stderr); return 99;
	}
	CHECK(submitted == 24 && fence_waits == 24 && bo_waits == 48);
	CHECK(created == 2 && closed == 2 && mapped == 2 && unmapped == 2);
	CHECK(peak == 2 * TFU_BO_SIZE && live_bytes == 0 && !fd_live && !sync_live);
	puts("PASS: actual TFU caller, raster pixels/guards, fences and lifecycle");
	return 0;
}
