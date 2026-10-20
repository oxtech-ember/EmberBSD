/* SPDX-License-Identifier: BSD-2-Clause */
/* Exercise the actual userspace checker with controlled DRM services. */
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The kernel UAPI supplies these on its supported BSD/Linux hosts. */
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

static int model_open(const char *, int, ...);
static int model_close(int);
static int model_ioctl(int, unsigned long, ...);
static void *model_mmap(void *, size_t, int, int, int, off_t);
static int model_munmap(void *, size_t);
#define open model_open
#define close model_close
#define ioctl model_ioctl
#define mmap model_mmap
#define munmap model_munmap
#include "checker-body.h"
#undef open
#undef close
#undef ioctl
#undef mmap
#undef munmap
#undef main

#define CHECK(c) do { if (!(c)) { \
	fprintf(stderr, "FAIL contract:%u: %s\n", (unsigned int)__LINE__, #c); exit(1); \
} } while (0)
#define MODEL_MAX 512
struct model_bo {
	uint32_t handle, gpu, *words;
	bool handle_live, map_live;
};
struct totals {
	atomic_uint cl, tfu, list72, list91, created, closed, mapped, unmapped;
	atomic_uint sync_created, sync_released;
};
static struct model_bo bos[MODEL_MAX];
static struct totals *totals;
static unsigned int allocated, open_bos, fd_live, sync_handle, live_bytes, sync_live;
static unsigned int peak_bytes, model_submitted, model_completed;
static const char *mode;

static void
release_process(void)
{

	/* Model process teardown on the checker's err(1) paths only. */
	for (unsigned int i = 0; i < allocated; i++) {
		free(bos[i].words);
		bos[i].words = NULL;
	}
}

static struct model_bo *
find(uint32_t handle)
{

	for (unsigned int i = allocated; i > 0; i--)
		if (bos[i - 1].handle == handle && bos[i - 1].handle_live)
			return &bos[i - 1];
	CHECK(false);
	return NULL;
}

static int
model_open(const char *path, int flags, ...)
{

	CHECK(strcmp(path, "model-v3d") == 0 && flags == (O_RDWR | O_CLOEXEC));
	CHECK(fd_live == 0 && open_bos == 0);
	fd_live = 1;
	return 77;
}

static int
model_close(int fd)
{

	CHECK(fd == 77 && fd_live == 1 && open_bos == 0);
	for (unsigned int i = 0; i < allocated; i++)
		CHECK(!bos[i].handle_live && !bos[i].map_live);
	CHECK(live_bytes == 0 && sync_live <= 1);
	if (strcmp(mode, "fd-close") == 0) { errno = EIO; return -1; }
	atomic_fetch_add(&totals->sync_released, sync_live);
	sync_live = 0;
	fd_live = 0;
	return 0;
}

static void *
model_mmap(void *address, size_t length, int protection, int flags, int fd,
    off_t offset)
{
	struct model_bo *bo = find((uint32_t)(offset / BO_SIZE));

	CHECK(address == NULL && length == BO_SIZE && fd == 77 && fd_live);
	CHECK(protection == (PROT_READ | PROT_WRITE) && flags == MAP_SHARED);
	CHECK(!bo->map_live && offset == (off_t)bo->handle * BO_SIZE);
	bo->map_live = true;
	atomic_fetch_add(&totals->mapped, 1);
	return bo->words;
}

static int
model_munmap(void *address, size_t length)
{

	CHECK(length == BO_SIZE);
	if (strcmp(mode, "unmap") == 0) { errno = EIO; return -1; }
	for (unsigned int i = 0; i < allocated; i++) {
		struct model_bo *bo = &bos[i];
		if (bo->words != address)
			continue;
		CHECK(bo->map_live && !bo->handle_live);
		bo->map_live = false;
		free(bo->words);
		bo->words = NULL;
		live_bytes -= BO_SIZE;
		atomic_fetch_add(&totals->unmapped, 1);
		return 0;
	}
	CHECK(false);
	return -1;
}

static void
submit_cl(const struct drm_v3d_submit_cl *a)
{
	const uint32_t *handles = (void *)(uintptr_t)a->bo_handles;
	struct model_bo *bo[CL_MAX_BOS] = {0};
	unsigned int count = a->bo_handle_count;

	CHECK(count == 5 || count == 72 || count == 91);
	CHECK(open_bos == count && a->flags == 0 && a->pad == 0);
	CHECK(a->perfmon_id == 0 && a->extensions == 0);
	CHECK(a->in_sync_bcl == sync_handle && a->in_sync_rcl == sync_handle);
	CHECK(a->out_sync == sync_handle && sync_handle != 0);
	for (unsigned int i = 0; i < count; i++) {
		bo[i] = find(handles[i]);
		CHECK(bo[i]->map_live);
		for (unsigned int j = 0; j < i; j++)
			CHECK(handles[i] != handles[j]);
	}
	CHECK(a->bcl_start == bo[0]->gpu && a->bcl_end > a->bcl_start);
	CHECK(a->bcl_end <= a->bcl_start + 4096);
	CHECK(a->rcl_start == bo[0]->gpu + 4096);
	CHECK(a->rcl_end > a->rcl_start && a->rcl_end <= a->rcl_start + 4096);
	CHECK(a->qma == bo[1]->gpu && a->qms == QV3D_TILE_ALLOC_SIZE);
	CHECK(a->qts == bo[2]->gpu);
	for (unsigned int i = 0; i < BO_SIZE / 4; i++) {
		CHECK(bo[3]->words[i] == GUARD);
		for (unsigned int b = 4; b < count; b++)
			CHECK(bo[b]->words[i] ==
			    (GUARD ^ (b - 4) * UINT32_C(0x01020305)));
		if (i >= QV3D_TILE_ALLOC_SIZE / 4)
			CHECK(bo[1]->words[i] == GUARD);
		if (i >= 4096 / 4)
			CHECK(bo[2]->words[i] == GUARD);
	}
	for (unsigned int i = 0; i < WORDS; i++)
		bo[3]->words[i] = QV3D_CLEAR_COLOR;
	if (strcmp(mode, "pixel") == 0) bo[3]->words[WORDS - 1] ^= 1;
	if (strcmp(mode, "output-guard") == 0) bo[3]->words[WORDS] ^= 1;
	if (strcmp(mode, "command") == 0) bo[0]->words[BO_SIZE / 4 - 1] ^= 1;
	if (strcmp(mode, "tile-alloc") == 0) bo[1]->words[BO_SIZE / 4 - 1] ^= 1;
	if (strcmp(mode, "tile-state") == 0) bo[2]->words[BO_SIZE / 4 - 1] ^= 1;
	if (strcmp(mode, "unused") == 0) bo[count - 1]->words[BO_SIZE / 4 - 1] ^= 1;
	model_submitted++;
	atomic_fetch_add(&totals->cl, 1);
	if (count == 72) atomic_fetch_add(&totals->list72, 1);
	if (count == 91) atomic_fetch_add(&totals->list91, 1);
}

static int
model_ioctl(int fd, unsigned long request, ...)
{
	va_list ap;
	void *arg;

	CHECK(fd == 77 && fd_live);
	va_start(ap, request);
	arg = va_arg(ap, void *);
	va_end(ap);
	if (request == DRM_IOCTL_V3D_CREATE_BO) {
		struct drm_v3d_create_bo *a = arg;
		struct model_bo *bo;
		CHECK(a->size == BO_SIZE && a->flags == 0);
		CHECK(allocated < MODEL_MAX);
		if (strcmp(mode, "partial") == 0 && open_bos == 17) {
			errno = ENOMEM; return -1;
		}
		bo = &bos[allocated++];
		bo->handle = allocated;
		bo->gpu = allocated * 65536;
		if (strcmp(mode, "duplicate") == 0 && allocated == 2)
			bo->handle = bos[0].handle;
		if (strcmp(mode, "overlap") == 0 && allocated == 2)
			bo->gpu = bos[0].gpu + 4096;
		if (strcmp(mode, "wrap") == 0 && allocated == 1)
			bo->gpu = UINT32_MAX - BO_SIZE + 2;
		bo->words = calloc(1, BO_SIZE);
		CHECK(bo->words != NULL);
		bo->handle_live = true;
		a->handle = bo->handle; a->offset = bo->gpu;
		open_bos++; live_bytes += BO_SIZE;
		if (live_bytes > peak_bytes) peak_bytes = live_bytes;
		atomic_fetch_add(&totals->created, 1);
		return 0;
	}
	if (request == DRM_IOCTL_V3D_MMAP_BO) {
		struct drm_v3d_mmap_bo *a = arg;
		(void)find(a->handle);
		a->offset = (uint64_t)a->handle * BO_SIZE;
		return 0;
	}
	if (request == DRM_IOCTL_V3D_GET_BO_OFFSET) {
		struct drm_v3d_get_bo_offset *a = arg;
		a->offset = find(a->handle)->gpu;
		return 0;
	}
	if (request == DRM_IOCTL_GEM_CLOSE) {
		const struct drm_gem_close *a = arg;
		struct model_bo *bo = find(a->handle);
		CHECK(bo->handle_live && model_completed == model_submitted);
		if (strcmp(mode, "close") == 0) { errno = EIO; return -1; }
		bo->handle_live = false; open_bos--;
		atomic_fetch_add(&totals->closed, 1);
		return 0;
	}
	if (request == DRM_IOCTL_SYNCOBJ_CREATE) {
		struct drm_syncobj_create *a = arg;
		CHECK(a->flags == DRM_SYNCOBJ_CREATE_SIGNALED);
		CHECK(sync_live == 0);
		sync_live = 1;
		atomic_fetch_add(&totals->sync_created, 1);
		a->handle = ++sync_handle;
		return 0;
	}
	if (request == DRM_IOCTL_SYNCOBJ_WAIT) {
		const struct drm_syncobj_wait *a = arg;
		CHECK(a->count_handles == 1 && a->timeout_nsec == 0);
		CHECK(a->flags == DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL);
		CHECK(*(uint32_t *)(uintptr_t)a->handles == sync_handle);
		if (strcmp(mode, "wait") == 0) { errno = ETIMEDOUT; return -1; }
		model_completed = model_submitted;
		return 0;
	}
	if (request == DRM_IOCTL_V3D_SUBMIT_CL) {
		struct drm_v3d_submit_cl *a = arg;
		if (strcmp(mode, "cap64") == 0 && a->bo_handle_count > 64) {
			errno = EINVAL; return -1;
		}
		if (strcmp(mode, "submit") == 0) { errno = EIO; return -1; }
		submit_cl(a);
		return 0;
	}
	if (request == DRM_IOCTL_V3D_SUBMIT_TFU) {
		const struct drm_v3d_submit_tfu *a = arg;
		if (a->flags == 1) { errno = EINVAL; return -1; }
		CHECK(a->flags == 0 && open_bos == 2);
		struct model_bo *dst = find(a->bo_handles[0]);
		const struct model_bo *src = find(a->bo_handles[1]);
		CHECK(a->iia == src->gpu && a->ioa == dst->gpu);
		memcpy(dst->words, src->words, IMAGE_SIZE);
		atomic_fetch_add(&totals->tfu, 1);
		return 0;
	}
	if (request == DRM_IOCTL_V3D_WAIT_BO) return 0;
	CHECK(false);
	return -1;
}

int
main(int argc, char **argv)
{
	char *args[] = {"cl-check", "model-v3d", NULL};

	CHECK(argc == 2);
	mode = argv[1];
	totals = mmap(NULL, sizeof(*totals), PROT_READ | PROT_WRITE,
	    MAP_ANON | MAP_SHARED, -1, 0);
	CHECK(totals != MAP_FAILED);
	memset(totals, 0, sizeof(*totals));
	CHECK(atexit(release_process) == 0);
	if (strcmp(mode, "pass") != 0 && strcmp(mode, "cap64") != 0) {
		render("model-v3d", 4, 72);
		fputs("injected failure was not detected\n", stderr);
		return 99;
	}
	CHECK(cl_check_main(2, args) == 0);
	CHECK(atomic_load(&totals->cl) == 333);
	CHECK(atomic_load(&totals->tfu) == 32);
	CHECK(atomic_load(&totals->list72) == 4);
	CHECK(atomic_load(&totals->list91) == 4);
	CHECK(peak_bytes == 91 * BO_SIZE && live_bytes == 0 && !fd_live);
	CHECK(atomic_load(&totals->created) == atomic_load(&totals->closed));
	CHECK(atomic_load(&totals->mapped) == atomic_load(&totals->unmapped));
	CHECK(atomic_load(&totals->created) == atomic_load(&totals->mapped));
	CHECK(atomic_load(&totals->sync_created) == 6);
	CHECK(atomic_load(&totals->sync_released) == 6);
	CHECK(munmap(totals, sizeof(*totals)) == 0);
	puts("PASS: actual CL checker caller, pixels, guards, unique lists and lifecycle");
	return 0;
}
