/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted. Controlled services around actual source. */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
static int model_open(const char *, int, ...);
static int model_close(int);
static off_t model_lseek(int, off_t, int);
static int model_ioctl(int, unsigned long, ...);
static void *model_mmap(void *, size_t, int, int, int, off_t);
static int model_munmap(void *, size_t);
static long model_sysconf(int);
static void *model_malloc(size_t);
static void model_free(void *);
static ssize_t model_write(int, const void *, size_t);
static int model_fsync(int);
static int model_unlink(const char *);
#define main snapshot_main
#define open model_open
#define close model_close
#define lseek model_lseek
#define ioctl model_ioctl
#define mmap model_mmap
#define munmap model_munmap
#define sysconf model_sysconf
#define malloc model_malloc
#define free model_free
#define write model_write
#define fsync model_fsync
#define unlink model_unlink
#include "snapshot-body.h"
#undef main
#undef open
#undef close
#undef lseek
#undef ioctl
#undef mmap
#undef munmap
#undef sysconf
#undef malloc
#undef free
#undef write
#undef fsync
#undef unlink
#include <stdarg.h>

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "FAIL contract: %s\n", #e);            \
			exit(1);                                               \
		}                                                              \
	} while (0)
static const char *mode;
static bool card, handle, output, image, prime, prime_exported;
static unsigned plane_calls, seq_calls, gem_closes, unmaps, closes, unlinks;
static size_t written;
static unsigned prime_exports, prime_closes, seeks;
static off_t bo_size;
static int prime_fd = 11;
static unsigned char *mapped, *allocated;
static long pagesize = 4096;
static uint64_t map_offset = 65536;

static bool
is(const char *s)
{
	return strcmp(mode, s) == 0;
}

static bool
fail(const char *s)
{
	if (!is(s))
		return false;
	errno = EIO;
	return true;
}

static long
model_sysconf(int key)
{
	CHECK(key == _SC_PAGESIZE);
	return pagesize;
}

static void *
model_malloc(size_t n)
{
	CHECK(n == SNAP_RGB_BYTES && allocated == NULL);
	if (fail("alloc"))
		return NULL;
	allocated = malloc(n);
	CHECK(allocated != NULL);
	memset(allocated, 0xa5, n);
	return allocated;
}

static void
model_free(void *p)
{
	CHECK(p == allocated);
	free(p);
	allocated = NULL;
}

static int
model_open(const char *path, int flags, ...)
{
	va_list ap;
	int access;

	if (strcmp(path, "model-card") == 0) {
		CHECK(flags == (O_RDONLY | O_CLOEXEC) && !card);
		if (fail("open"))
			return -1;
		card = true;
		return 10;
	}
	CHECK(strcmp(path, "model.ppm") == 0 && !card && !handle &&
	      mapped == NULL && !prime);
	CHECK(flags == (O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC));
	va_start(ap, flags);
	access = va_arg(ap, int);
	va_end(ap);
	CHECK(access == 0600 && !image && !output);
	if (is("exists")) {
		errno = EEXIST;
		return -1;
	}
	if (fail("create"))
		return -1;
	output = image = true;
	return 20;
}

static int
model_close(int fd)
{
	closes++;
	if (fd == prime_fd && prime) {
		prime_closes++;
		prime = false;
		return fail("close-prime") ? -1 : 0;
	}
	if (fd == 10) {
		CHECK(card);
		card = handle = false;
		return fail("close-card") ? -1 : 0;
	}
	CHECK(fd == 20 && output);
	output = false;
	return fail("close-output") ? -1 : 0;
}

static off_t
model_lseek(int fd, off_t offset, int whence)
{

	CHECK(fd == prime_fd && prime && handle && card);
	CHECK(offset == 0 && whence == SEEK_END);
	seeks++;
	if (fail("bo-seek"))
		return (off_t)-1;
	return bo_size;
}

static int
model_ioctl(int fd, unsigned long cmd, ...)
{
	struct drm_gem_close *g;
	va_list ap;
	void *arg;

	CHECK(fd == 10 && card);
	va_start(ap, cmd);
	arg = va_arg(ap, void *);
	va_end(ap);
	if (cmd == DRM_IOCTL_VERSION) {
		struct drm_version *v = arg;
		if (fail("version"))
			return -1;
		CHECK(v->name_len == 7);
		memcpy(v->name, is("driver") ? "bad" : "v3d", 3);
		v->name_len = 3;
		return 0;
	}
	if (cmd == DRM_IOCTL_MODE_GETPLANE) {
		struct drm_mode_get_plane *p = arg;
		plane_calls++;
		CHECK(p->plane_id == 31 && p->count_format_types == 1);
		if ((plane_calls == 1 && fail("plane-first")) ||
		    (plane_calls == 2 && fail("plane-before")) ||
		    (plane_calls == 3 && fail("plane-after")))
			return -1;
		*(uint32_t *)(uintptr_t)p->format_type_ptr =
		    is("format") ? DRM_FORMAT_ARGB8888 : DRM_FORMAT_XRGB8888;
		p->fb_id = 42;
		if (is("blank"))
			p->fb_id = 0;
		if ((is("changed-before") && plane_calls == 2) ||
		    (is("changed-after") && plane_calls == 3))
			p->fb_id = 99;
		p->crtc_id = 35;
		if (is("no-crtc"))
			p->crtc_id = 0;
		if (is("changed-crtc") && plane_calls == 3)
			p->crtc_id = 36;
		p->count_format_types = is("two-formats") ? 2 : 1;
		return 0;
	}
	if (cmd == DRM_IOCTL_MODE_GETFB) {
		struct drm_mode_fb_cmd *f = arg;
		CHECK(f->fb_id == 42);
		if (fail("get-fb"))
			return -1;
		f->handle = is("no-handle") ? 0 : 7;
		handle = f->handle != 0;
		f->width = is("width") ? 1921 : 1920;
		f->height = is("height") ? 1079 : 1080;
		f->pitch = is("pitch") ? 7684 : 7680;
		f->depth = is("depth") ? 32 : 24;
		f->bpp = is("bpp") ? 24 : 32;
		return 0;
	}
	if (cmd == DRM_IOCTL_PRIME_HANDLE_TO_FD) {
		struct drm_prime_handle *p = arg;
		CHECK(handle && p->handle == 7 && !prime);
		CHECK(p->flags == DRM_CLOEXEC && p->fd == -1);
		prime_exports++;
		if (fail("prime-export"))
			return -1;
		p->fd = is("prime-negative-fd") ? -2 : prime_fd;
		prime = prime_exported = p->fd >= 0;
		return 0;
	}
	if (cmd == DRM_IOCTL_V3D_WAIT_BO) {
		struct drm_v3d_wait_bo *w = arg;
		CHECK(handle && w->handle == 7);
		CHECK(w->timeout_ns == UINT64_C(1000000000));
		if (is("wait-timeout")) {
			errno = ETIMEDOUT;
			return -1;
		}
		return fail("wait") ? -1 : 0;
	}
	if (cmd == DRM_IOCTL_MODE_MAP_DUMB) {
		struct drm_mode_map_dumb *m = arg;
		CHECK(handle && m->handle == 7);
		if (fail("map-offset"))
			return -1;
		m->offset = map_offset;
		return 0;
	}
	if (cmd == DRM_IOCTL_CRTC_GET_SEQUENCE) {
		struct drm_crtc_get_sequence *s = arg;
		seq_calls++;
		CHECK(s->crtc_id == 35);
		if ((seq_calls == 1 && fail("seq-before")) ||
		    (seq_calls == 2 && fail("seq-after")))
			return -1;
		s->active = (is("inactive-before") && seq_calls == 1) ||
				    (is("inactive-after") && seq_calls == 2)
				? 0
				: 1;
		s->sequence =
		    seq_calls == 2 ? is("seq-backwards") ? 9 : 11 : 10;
		s->sequence_ns =
		    seq_calls == 2 ? is("time-backwards") ? 99 : 200 : 100;
		return 0;
	}
	CHECK(cmd == DRM_IOCTL_GEM_CLOSE);
	g = arg;
	CHECK(handle && g->handle == 7);
	gem_closes++;
	if (fail("gem-close"))
		return -1;
	handle = false;
	return 0;
}

static void *
model_mmap(void *p, size_t n, int prot, int flags, int fd, off_t off)
{
	size_t i;

	CHECK(p == NULL && prot == PROT_READ && flags == MAP_SHARED &&
	      fd == 10 && card && handle);
	CHECK(n == (size_t)bo_size && seeks == 1 && prime);
	CHECK(n >= SNAP_BYTES && n <= SNAP_BO_MAX &&
	    (n & ((size_t)pagesize - 1)) == 0);
	CHECK((uint64_t)off == map_offset);
	if (fail("mmap"))
		return MAP_FAILED;
	mapped = malloc(n);
	CHECK(mapped != NULL);
	memset(mapped, 0xcc, n);
	for (i = 0; i < (size_t)SNAP_WIDTH * SNAP_HEIGHT; i++) {
		mapped[4 * i] = (uint8_t)i;
		mapped[4 * i + 1] = (uint8_t)(i / 7);
		mapped[4 * i + 2] = (uint8_t)(i / 13);
		mapped[4 * i + 3] = i & 1 ? 0 : 255;
	}
	return mapped;
}

static int
model_munmap(void *p, size_t n)
{
	size_t i;

	CHECK(p == mapped && n == (size_t)bo_size);
	for (i = SNAP_BYTES; i < n; i++)
		CHECK(mapped[i] == 0xcc);
	unmaps++;
	if (fail("munmap"))
		return -1;
	free(mapped);
	mapped = NULL;
	return 0;
}

static ssize_t
model_write(int fd, const void *p, size_t n)
{
	static const char header[] = "P6\n1920 1080\n255\n";
	const unsigned char *bytes = p;
	size_t j, at, pos, pixel;
	unsigned char expected;

	CHECK(fd == 20 && output && image);
	if ((written == 0 && fail("write-header")) ||
	    (written != 0 && fail("write-rgb")))
		return -1;
	if (is("write-zero"))
		return 0;
	if (is("eintr") && written == 0) {
		mode = "pass";
		errno = EINTR;
		return -1;
	}
	if (is("short") && n > 997)
		n = 997;
	for (j = 0; j < n; j++) {
		at = written + j;
		if (at < sizeof(header) - 1)
			CHECK(bytes[j] == (unsigned char)header[at]);
		else {
			pos = at - (sizeof(header) - 1);
			pixel = pos / 3;
			expected =
			    (uint8_t)(pos % 3 == 0   ? pixel / 13
				      : pos % 3 == 1 ? pixel / 7
						     : pixel);
			CHECK(bytes[j] == expected);
		}
	}
	written += n;
	return (ssize_t)n;
}

static int
model_fsync(int fd)
{
	CHECK(fd == 20 && output && written == 17 + SNAP_RGB_BYTES);
	return fail("fsync") ? -1 : 0;
}

static int
model_unlink(const char *path)
{
	CHECK(strcmp(path, "model.ppm") == 0 && image && !output);
	unlinks++;
	image = false;
	return 0;
}

int
main(int argc, char **argv)
{
	bool success;
	int result;

	CHECK(argc == 2);
	mode = argv[1];
	if (is("page64") || is("page64-padded"))
		pagesize = 65536;
	if (is("bad-page"))
		pagesize = 16384;
	if (is("unaligned"))
		map_offset++;
	if (is("offset-overflow"))
		map_offset = (uint64_t)INT64_MAX & ~(uint64_t)(pagesize - 1);
	if (is("high-offset"))
		map_offset = ((uint64_t)INT64_MAX - SNAP_BYTES - 65536) &
			     ~(uint64_t)(pagesize - 1);
	bo_size = (off_t)((SNAP_BYTES + (size_t)pagesize - 1) &
	    ~((size_t)pagesize - 1));
	if (is("page4-padded") || is("page64-padded"))
		bo_size += pagesize;
	if (is("bo-max"))
		bo_size = SNAP_BO_MAX;
	if (is("bo-zero"))
		bo_size = 0;
	if (is("bo-small"))
		bo_size = (off_t)SNAP_BYTES - pagesize;
	if (is("bo-unaligned"))
		bo_size++;
	if (is("bo-oversize"))
		bo_size = SNAP_BO_MAX + pagesize;
	if (is("bo-negative"))
		bo_size = -2;
	if (is("prime-zero-fd"))
		prime_fd = 0;
	success = is("pass") || is("page64") || is("high-offset") ||
		       is("short") || is("eintr") || is("page4-padded") ||
	    is("page64-padded") || is("bo-max") || is("prime-zero-fd");
	result = snapshot("model-card", 31, "model.ppm");
	CHECK(result == (success ? 0 : 1));
	CHECK(!card && !handle && !output && !prime && allocated == NULL);
	CHECK(prime_closes == (prime_exported ? 1U : 0U));
	if (is("prime-export") || is("prime-negative-fd"))
		CHECK(prime_exports == 1 && seeks == 0 && prime_closes == 0);
	if (is("bo-seek") || is("bo-zero") || is("bo-small") ||
	    is("bo-unaligned") || is("bo-oversize") || is("bo-negative"))
		CHECK(prime_exports == 1 && seeks == 1 && prime_closes == 1 &&
		    unmaps == 0 && written == 0);
	CHECK(image == success);
	if (success) {
		CHECK(written == 17 + SNAP_RGB_BYTES);
		CHECK(gem_closes == 1 && unmaps == 1 && mapped == NULL);
	}
	if (is("exists"))
		CHECK(unlinks == 0);
	/* An injected munmap failure leaves its mapping until process exit. */
	if (is("munmap")) {
		CHECK(unmaps == 1 && mapped != NULL);
		free(mapped);
		mapped = NULL;
	}
	CHECK(mapped == NULL);
	puts("PASS controlled snapshot scenario");
	return 0;
}
