/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD, AI-assisted. Actual caller with controlled DRM services. */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>

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
static long model_sysconf(int);
#define main map_checker_main
#define open model_open
#define close model_close
#define ioctl model_ioctl
#define mmap model_mmap
#define munmap model_munmap
#define sysconf model_sysconf
#include "mmap-check-body.h"
#undef main
#undef open
#undef close
#undef ioctl
#undef mmap
#undef munmap
#undef sysconf

#define CHECK(e) do { if (!(e)) { \
	fprintf(stderr, "FAIL contract: %s\n", #e); exit(1); \
} } while (0)

struct object {
	unsigned char *data;
	size_t size;
	uint64_t offset;
	unsigned int maps;
	bool handle;
};
struct mapping {
	void *address;
	size_t length;
	struct object *object;
	bool live, detached;
	bool tail_changed;
	unsigned char tail;
};
static struct object objects[3];
static struct mapping mappings[24];
static const char *mode;
static size_t model_page = 4096;
static bool owner, foreign;
static unsigned int opens, creates, map_ioctls, full_maps, reads, writes;
static unsigned int releases, unmaps, closes, negative_calls, lifecycle;
static unsigned int frame_reads;
static bool one_failure;

static bool
is(const char *name)
{

	return strcmp(mode, name) == 0;
}

static bool
fail_number(const char *prefix, unsigned int n)
{
	char name[40];

	(void)snprintf(name, sizeof(name), "%s%u", prefix, n);
	if (!is(name))
		return false;
	errno = EIO;
	return true;
}

static long
model_sysconf(int key)
{

	CHECK(key == _SC_PAGESIZE);
	return is("page-invalid") ? 8192 : (long)model_page;
}

static int
model_open(const char *path, int flags, ...)
{

	CHECK(strcmp(path, "model-v3d") == 0);
	CHECK(flags == (O_RDWR | O_CLOEXEC));
	opens++;
	if (fail_number("open", opens))
		return -1;
	CHECK(opens <= 2);
	if (opens == 1) owner = true;
	else foreign = true;
	return opens == 1 ? 77 : 78;
}

static int
model_ioctl(int fd, unsigned long request, ...)
{
	va_list ap;
	void *argument;

	CHECK(fd == 77 && owner);
	va_start(ap, request);
	argument = va_arg(ap, void *);
	va_end(ap);
	if (request == DRM_IOCTL_VERSION) {
		struct drm_version *v = argument;

		CHECK(v->name_len == 7);
		if (is("version")) { errno = EIO; return -1; }
		memcpy(v->name, is("driver") ? "bad" : "v3d", 3);
		v->name_len = 3;
		return 0;
	}
	if (request == DRM_IOCTL_V3D_CREATE_BO) {
		struct drm_v3d_create_bo *a = argument;

		creates++;
		if (fail_number("create", creates))
			return -1;
		CHECK(creates <= 3);
		struct object *o = &objects[creates - 1];
		size_t size = creates == 1 ? 3 * model_page :
		    creates == 2 ? 5 * model_page : LARGE_BYTES;

		CHECK(a->size == size && a->flags == 0);
		o->data = calloc(1, size);
		CHECK(o->data != NULL);
		o->size = size;
		o->offset = UINT64_C(0x110000000) + creates * UINT64_C(0x2000000);
		o->handle = true;
		a->handle = creates;
		a->offset = 0x100000 + creates * 0x1000000;
		return 0;
	}
	if (request == DRM_IOCTL_V3D_MMAP_BO) {
		struct drm_v3d_mmap_bo *a = argument;

		CHECK(a->handle >= 1 && a->handle <= 3);
		CHECK(objects[a->handle - 1].handle);
		map_ioctls++;
		if (fail_number("map-ioctl", map_ioctls))
			return -1;
		a->offset = objects[a->handle - 1].offset;
		return 0;
	}
	if (request == DRM_IOCTL_GEM_CLOSE) {
		struct drm_gem_close *a = argument;

		CHECK(a->handle >= 1 && a->handle <= 3);
		struct object *o = &objects[a->handle - 1];

		CHECK(o->handle);
		if (is("handle-close") && !one_failure) {
			one_failure = true; errno = EIO; return -1;
		}
		o->handle = false;
		releases++;
		return 0;
	}
	CHECK(false);
	return -1;
}

static void *
model_register(struct object *object, size_t n, bool detached, bool copy)
{

	for (unsigned int i = 0; i < 24; i++) {
		struct mapping *m = &mappings[i];

		if (m->live)
			continue;
		m->address = detached ? calloc(1, n) : object->data;
		CHECK(m->address != NULL);
		if (copy)
			memcpy(m->address, object->data, n);
		m->object = object;
		m->length = n;
		m->detached = detached;
		m->tail_changed = false;
		m->live = true;
		if (object != NULL)
			object->maps++;
		return m->address;
	}
	CHECK(false);
	return MAP_FAILED;
}

static void *
model_mmap(void *hint, size_t n, int prot, int flags, int fd, off_t offset)
{
	struct object *object = NULL;
	const char *negative = NULL;
	int expected = EINVAL;

	CHECK(hint == NULL && flags == MAP_SHARED);
	CHECK(fd == 77 ? owner : fd == 78 && foreign);
	CHECK(prot == PROT_READ || prot == (PROT_READ | PROT_WRITE));
	for (unsigned int i = 0; i < 3; i++) {
		if ((uint64_t)offset >= objects[i].offset && objects[i].data &&
		    (uint64_t)offset - objects[i].offset < objects[i].size) {
			object = &objects[i];
			break;
		}
	}
	if (object == NULL) negative = "unknown";
	else if ((uint64_t)offset != object->offset) negative = "interior";
	else if (n > object->size) negative = "oversize";
	else if (fd == 78) { negative = "unowned"; expected = EACCES; }
	if (negative != NULL) {
		negative_calls++;
		char accepted[40], wrong[40];

		(void)snprintf(accepted, sizeof(accepted), "%s-accepted", negative);
		(void)snprintf(wrong, sizeof(wrong), "%s-errno", negative);
		if (is(accepted))
			return model_register(NULL, n, true, false);
		errno = is(wrong) ? EIO : expected;
		return MAP_FAILED;
	}
	CHECK(object->handle);
	CHECK(n > 0 && n <= object->size);
	if (n == object->size) {
		full_maps++;
		if (fail_number("full-map", full_maps))
			return MAP_FAILED;
	} else if (prot == PROT_READ) {
		reads++;
		if (n == FRAME_BYTES)
			frame_reads++;
		if (is("read-map")) { errno = EIO; return MAP_FAILED; }
		if (is("anon-read") || (is("frame-anon") && n == FRAME_BYTES))
			return model_register(object, n, true, false);
		if (is("wrong-BO"))
			return model_register(&objects[1], n, false, false);
	} else {
		writes++;
		if (is("write-map")) { errno = EIO; return MAP_FAILED; }
		if (is("copy-prefix"))
			return model_register(object, n, true, true);
	}
	void *p = model_register(object, n, false, false);
	if (is("tail-byte") && n < object->size &&
	    prot == (PROT_READ | PROT_WRITE) && writes <= 5) {
		for (unsigned int i = 0; i < 24; i++) {
			struct mapping *m = &mappings[i];

			if (!m->live || m->address != p || m->length != n)
				continue;
			m->tail = object->data[n];
			m->tail_changed = true;
			object->data[n] ^= 1;
			break;
		}
	}

	if (is("last-byte") && prot == PROT_READ)
		((unsigned char *)p)[n - 1] ^= 1;
	if (writes == 6)
		lifecycle++;
	return p;
}

static int
model_munmap(void *address, size_t n)
{

	for (unsigned int i = 0; i < 24; i++) {
		struct mapping *m = &mappings[i];

		if (!m->live || m->address != address || m->length != n)
			continue;
		if ((is("pass") || is("page64")) && writes == 6 &&
		    m->object == &objects[0] && n == model_page)
			CHECK(!owner);
		if (m->object != NULL) {
			CHECK(m->object->maps > 0);
			m->object->maps--;
			if (m->tail_changed)
				m->object->data[n] = m->tail;
		}
		if (m->detached)
			free(m->address);
		m->live = false;
		unmaps++;
		if (is("unmap") && !one_failure) {
			one_failure = true; errno = EIO; return -1;
		}
		return 0;
	}
	CHECK(false);
	return -1;
}

static int
model_close(int fd)
{

	CHECK(fd == 77 ? owner : fd == 78 && foreign);
	closes++;
	if (fd == 77) {
		owner = false;
		if (is("owner-zeroes")) {
			for (unsigned int i = 0; i < 3; i++)
				memset(objects[i].data, 0, objects[i].size);
		}
		for (unsigned int i = 0; i < 3; i++)
			objects[i].handle = false;
	} else {
		foreign = false;
	}
	if (is("close") && !one_failure) {
		one_failure = true; errno = EIO; return -1;
	}
	return 0;
}

int
main(int argc, char **argv)
{
	char *args[] = {"mmap-check", "model-v3d", NULL};
	bool success;
	int result;

	CHECK(argc == 2);
	mode = argv[1];
	if (is("page64"))
		model_page = 65536;
	success = is("pass") || is("page64");
	result = map_checker_main(is("usage") ? 1 : 2, args);
	if (is("usage")) CHECK(result == 2);
	else CHECK(result == (success ? 0 : 1));
	CHECK(!owner && !foreign);
	for (unsigned int i = 0; i < 24; i++)
		CHECK(!mappings[i].live);
	for (unsigned int i = 0; i < 3; i++) {
		CHECK(!objects[i].handle && objects[i].maps == 0);
		free(objects[i].data);
	}
	if (success) {
		CHECK(creates == 3 && full_maps == 3 && releases == 3);
		CHECK(reads == 5 && writes == 6 && negative_calls == 10);
		CHECK(frame_reads == 1);
		CHECK(unmaps == 14 && closes == 2 && lifecycle == 1);
	}
	puts("PASS controlled mmap scenario");
	return 0;
}
