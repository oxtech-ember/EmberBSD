/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted. Native GEM prefix mapping acceptance. */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <drm.h>
#include <v3d_drm.h>

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

_Static_assert(sizeof(off_t) >= 8, "64-bit GEM offsets required");

#define LARGE_BYTES (16U * 1024U * 1024U)
#define FRAME_BYTES (1920U * 1080U * 4U)

struct map_bo {
	uint32_t handle;
	size_t size;
	uint64_t offset;
	volatile unsigned char *full;
	unsigned int seed;
	size_t changed[4];
	unsigned char values[4];
	unsigned int nchanged;
};

static unsigned char
map_pattern(unsigned int seed, size_t byte)
{

	return (unsigned char)(1 + ((byte * 37 + byte / 251 + seed * 53) % 255));
}

static unsigned char
map_expected(const struct map_bo *bo, size_t byte)
{

	for (unsigned int i = 0; i < bo->nchanged; i++) {
		if (bo->changed[i] == byte)
			return bo->values[i];
	}
	return map_pattern(bo->seed, byte);
}

static int
map_verify(const struct map_bo *bo, volatile const unsigned char *memory,
    size_t length, const char *stage)
{

	for (size_t i = 0; i < length; i++) {
		unsigned char actual = memory[i];
		unsigned char expected = map_expected(bo, i);

		if (actual != expected) {
			fprintf(stderr, "FAIL: stage=%s byte=%zu actual=%u "
			    "expected=%u\n", stage, i, actual, expected);
			return EIO;
		}
	}
	return 0;
}

static int
map_close_handle(int fd, struct map_bo *bo)
{
	struct drm_gem_close args = {.handle = bo->handle};

	if (bo->handle == 0)
		return 0;
	if (ioctl(fd, DRM_IOCTL_GEM_CLOSE, &args) == -1)
		return errno;
	bo->handle = 0;
	return 0;
}

static int
map_allocate(int fd, struct map_bo *bo, size_t size, unsigned int seed)
{
	struct drm_v3d_create_bo create = {.size = (uint32_t)size};
	struct drm_v3d_mmap_bo args = {0};

	bo->size = size;
	bo->seed = seed;
	bo->full = MAP_FAILED;
	if (ioctl(fd, DRM_IOCTL_V3D_CREATE_BO, &create) == -1)
		return errno;
	bo->handle = create.handle;
	if (bo->handle == 0)
		return EIO;
	args.handle = bo->handle;
	if (ioctl(fd, DRM_IOCTL_V3D_MMAP_BO, &args) == -1)
		return errno;
	if (args.offset == 0 || args.offset > INT64_MAX - size - 65536)
		return EOVERFLOW;
	bo->offset = args.offset;
	bo->full = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED,
	    fd, (off_t)bo->offset);
	if (bo->full == MAP_FAILED)
		return errno;
	for (size_t i = 0; i < size; i++)
		bo->full[i] = map_pattern(seed, i);
	return map_verify(bo, bo->full, size, "full-initial");
}

static int
map_refuse(int fd, uint64_t offset, size_t length, int expected,
    const char *stage)
{
	void *memory;
	int error;

	errno = 0;
	memory = mmap(NULL, length, PROT_READ, MAP_SHARED, fd, (off_t)offset);
	error = errno;
	if (memory != MAP_FAILED) {
		fprintf(stderr, "FAIL: stage=%s unexpected mapping\n", stage);
		if (munmap(memory, length) == -1)
			fprintf(stderr, "FAIL: stage=unexpected-unmap errno=%d\n",
			    errno);
		return EIO;
	}
	if (error != expected) {
		fprintf(stderr, "FAIL: stage=%s errno=%d expected=%d\n",
		    stage, error, expected);
		return error != 0 ? error : EIO;
	}
	return 0;
}

static int
map_prefix(int fd, struct map_bo *bo, size_t length, size_t page,
    const struct map_bo *other)
{
	volatile unsigned char *prefix;
	int error = 0;

	prefix = mmap(NULL, length, PROT_READ, MAP_SHARED, fd,
	    (off_t)bo->offset);
	if (prefix == MAP_FAILED)
		return errno;
	error = map_verify(bo, prefix, length, "read-prefix");
	if (munmap((void *)prefix, length) == -1 && error == 0)
		error = errno;
	if (error != 0)
		return error;
	prefix = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
	    (off_t)bo->offset);
	if (prefix == MAP_FAILED)
		return errno;
	error = map_verify(bo, prefix, length, "write-prefix-initial");
	if (error != 0)
		goto out;
	bo->nchanged = 0;
	bo->changed[0] = 0;
	bo->changed[1] = length - 1;
	bo->changed[2] = page - 1;
	bo->changed[3] = length > page ? page : page / 2;
	bo->nchanged = 4;
	for (unsigned int i = 0; i < bo->nchanged; i++) {
		bo->values[i] = (unsigned char)(map_pattern(bo->seed,
		    bo->changed[i]) ^ 0x5a);
		prefix[bo->changed[i]] = bo->values[i];
	}
	error = map_verify(bo, bo->full, bo->size, "prefix-to-full-guards");
	if (error != 0)
		goto out;
	for (unsigned int i = 0; i < bo->nchanged; i++) {
		bo->values[i] ^= 0xa5;
		bo->full[bo->changed[i]] = bo->values[i];
	}
	error = map_verify(bo, prefix, length, "full-to-prefix");
	if (error == 0)
		error = map_verify(other, other->full, other->size, "other-BO");
out:
	if (munmap((void *)prefix, length) == -1 && error == 0)
		error = errno;
	return error;
}

static int
map_check(const char *node)
{
	struct map_bo bos[3] = {{.full = MAP_FAILED}, {.full = MAP_FAILED},
	    {.full = MAP_FAILED}};
	struct drm_version version = {0};
	volatile unsigned char *held = MAP_FAILED;
	char name[8] = {0};
	const char *stage = "page-size";
	long pagesize;
	size_t page = 0;
	int fd = -1, foreign = -1, error = 0, cleanup;

	pagesize = sysconf(_SC_PAGESIZE);
	if (pagesize != 4096 && pagesize != 65536) {
		error = EINVAL;
		goto out;
	}
	page = (size_t)pagesize;
	stage = "open";
	fd = open(node, O_RDWR | O_CLOEXEC);
	if (fd == -1) {
		error = errno;
		goto out;
	}
	stage = "driver";
	version.name = name;
	version.name_len = sizeof(name) - 1;
	if (ioctl(fd, DRM_IOCTL_VERSION, &version) == -1) {
		error = errno;
		goto out;
	}
	if (version.name_len != 3 || memcmp(name, "v3d", 3) != 0) {
		error = ENODEV;
		goto out;
	}
	stage = "independent-file";
	foreign = open(node, O_RDWR | O_CLOEXEC);
	if (foreign == -1) {
		error = errno;
		goto out;
	}
	for (unsigned int i = 0; i < 3; i++) {
		size_t size = i == 0 ? 3 * page : i == 1 ? 5 * page : LARGE_BYTES;

		stage = "allocate";
		error = map_allocate(fd, &bos[i], size, i + 1);
		if (error != 0)
			goto out;
	}
	for (unsigned int i = 0; i < 3; i++) {
		struct map_bo *bo = &bos[i];
		const struct map_bo *other = &bos[(i + 1) % 3];

		for (unsigned int n = 1; n <= 2; n++) {
			size_t length = i == 2 ? FRAME_BYTES : n * page;

			stage = "prefix-alias";
			error = map_prefix(fd, bo, length, page, other);
			if (error != 0)
				goto out;
			printf("PASS: BO=%u size=%zu prefix=%zu every-byte "
			    "read/RW alias and tail guards\n", i, bo->size, length);
			if (i == 2)
				break;
			/* Each prefix starts with an independent complete pattern. */
			for (size_t byte = 0; byte < bo->size; byte++)
				bo->full[byte] = map_pattern(bo->seed, byte);
			bo->nchanged = 0;
		}
		stage = "oversize";
		error = map_refuse(fd, bo->offset, bo->size + page, EINVAL, stage);
		if (error != 0)
			goto out;
		stage = "unowned";
		error = map_refuse(foreign, bo->offset, page, EACCES, stage);
		if (error != 0)
			goto out;
		stage = "interior-offset";
		error = map_refuse(fd, bo->offset + page, page, EINVAL, stage);
		if (error != 0)
			goto out;
	}
	stage = "unknown-offset";
	error = map_refuse(fd, 0, page, EINVAL, stage);
	if (error != 0)
		goto out;
	stage = "map-lifetime";
	held = mmap(NULL, page, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
	    (off_t)bos[0].offset);
	if (held == MAP_FAILED) {
		error = errno;
		goto out;
	}
	for (unsigned int i = 0; i < 3; i++) {
		error = map_close_handle(fd, &bos[i]);
		if (error != 0)
			goto out;
	}
	cleanup = close(fd);
	fd = -1;
	if (cleanup == -1) {
		error = errno;
		goto out;
	}
	/* Mappings retain the objects after the owner file and handles close. */
	held[page - 1] = 0x69;
	bos[0].changed[0] = page - 1;
	bos[0].values[0] = 0x69;
	bos[0].nchanged = 1;
	stage = "closed-owner-alias";
	for (unsigned int i = 0; i < 3 && error == 0; i++)
		error = map_verify(&bos[i], bos[i].full, bos[i].size, stage);
out:
	if (error != 0)
		fprintf(stderr, "FAIL: stage=%s errno=%d\n", stage, error);
	if (held != MAP_FAILED && munmap((void *)held, page) == -1) {
		fprintf(stderr, "FAIL: stage=cleanup-held errno=%d\n", errno);
		error = EIO;
	}
	for (unsigned int i = 0; i < 3; i++) {
		if (bos[i].full != MAP_FAILED &&
		    munmap((void *)bos[i].full, bos[i].size) == -1) {
			fprintf(stderr, "FAIL: stage=cleanup-map errno=%d\n", errno);
			error = EIO;
		}
		if (fd != -1 && map_close_handle(fd, &bos[i]) != 0) {
			fprintf(stderr, "FAIL: stage=cleanup-handle errno=%d\n", errno);
			error = EIO;
		}
	}
	if (foreign != -1 && close(foreign) == -1) {
		fprintf(stderr, "FAIL: stage=cleanup-foreign errno=%d\n", errno);
		error = EIO;
	}
	if (fd != -1 && close(fd) == -1) {
		fprintf(stderr, "FAIL: stage=cleanup-file errno=%d\n", errno);
		error = EIO;
	}
	if (error == 0)
		puts("PASS: native GEM base-prefix mappings, alias lifetime and "
		    "negative admission; no GPU jobs or display changes");
	return error == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

int
main(int argc, char **argv)
{

	if (argc != 2) {
		fprintf(stderr, "Usage: %s NATIVE_V3D_DRM_NODE\n", argv[0]);
		return 2;
	}
	setvbuf(stdout, NULL, _IOLBF, 0);
	return map_check(argv[1]);
}
