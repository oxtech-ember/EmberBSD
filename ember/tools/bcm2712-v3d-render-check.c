/* Origin: EmberBSD native V3D userspace acceptance, 2026-10-11. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <drm.h>
#include <v3d_drm.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define IMAGE_SIZE (64 * 64 * 4)
#define BO_SIZE (IMAGE_SIZE + 4096)
#define WORDS (IMAGE_SIZE / sizeof(uint32_t))
#define GUARD UINT32_C(0xc35a69a5)
struct buffer {
	uint32_t handle, gpu;
	uint64_t offset;
	uint32_t *map;
};

static void
check(int condition, const char *message)
{
	if (!condition)
		errx(1, "%s", message);
}

static void
close_handle(int fd, uint32_t handle)
{
	struct drm_gem_close close = { .handle = handle };

	if (ioctl(fd, DRM_IOCTL_GEM_CLOSE, &close) == -1)
		err(1, "GEM_CLOSE");
}

static struct buffer
allocate(int fd)
{
	struct drm_v3d_create_bo create = { .size = BO_SIZE };
	struct drm_v3d_mmap_bo mapping;
	struct drm_v3d_get_bo_offset offset;
	struct buffer bo;
	size_t i;

	if (ioctl(fd, DRM_IOCTL_V3D_CREATE_BO, &create) == -1)
		err(1, "CREATE_BO");
	check(create.handle != 0 && create.offset != 0, "zero handle/address");
	mapping = (struct drm_v3d_mmap_bo){ .handle = create.handle };
	if (ioctl(fd, DRM_IOCTL_V3D_MMAP_BO, &mapping) == -1)
		err(1, "MMAP_BO");
	offset = (struct drm_v3d_get_bo_offset){ .handle = create.handle };
	if (ioctl(fd, DRM_IOCTL_V3D_GET_BO_OFFSET, &offset) == -1)
		err(1, "GET_BO_OFFSET");
	check(offset.offset == create.offset, "inconsistent GPU address");
	bo = (struct buffer){ .handle = create.handle, .gpu = create.offset,
	    .offset = mapping.offset };
	bo.map = mmap(NULL, BO_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
	    fd, (off_t)mapping.offset);
	if (bo.map == MAP_FAILED)
		err(1, "mmap GEM");
	for (i = 0; i < BO_SIZE / sizeof(uint32_t); i++)
		check(bo.map[i] == 0, "new GEM allocation is not zeroed");
	return bo;
}

static uint32_t
pattern(unsigned int iteration, size_t word)
{
	return UINT32_C(0xa5335ac3) ^ iteration * UINT32_C(0x1020305) ^
	    (uint32_t)word * UINT32_C(0x12345);
}

static void
copy_test(int fd, unsigned int count)
{
	struct buffer src = allocate(fd), dst = allocate(fd);
	struct drm_v3d_submit_tfu tfu = {
		.icfg = 0x001d0001, .iia = src.gpu, .iis = 64,
		.ioa = dst.gpu, .ios = 0x00400040,
		.bo_handles = { dst.handle, src.handle },
		.v71 = { .ioc = 0x00400000 }
	};
	struct drm_v3d_wait_bo wait = { .handle = dst.handle };
	unsigned int n;
	size_t i;

	check(src.gpu != dst.gpu, "overlapping GPU virtual addresses");
	/* Invalid input must fail without poisoning the next valid operation. */
	tfu.flags = 1;
	check(ioctl(fd, DRM_IOCTL_V3D_SUBMIT_TFU, &tfu) == -1 && errno == EINVAL,
	    "unsupported TFU flags accepted");
	tfu.flags = 0;
	for (n = 0; n < count; n++) {
		for (i = 0; i < BO_SIZE / sizeof(uint32_t); i++) {
			src.map[i] = i < WORDS ? pattern(n, i) : GUARD;
			dst.map[i] = GUARD;
		}
		if (ioctl(fd, DRM_IOCTL_V3D_SUBMIT_TFU, &tfu) == -1)
			err(1, "SUBMIT_TFU iteration %u", n);
		if (ioctl(fd, DRM_IOCTL_V3D_WAIT_BO, &wait) == -1)
			err(1, "WAIT_BO after synchronous TFU");
		for (i = 0; i < BO_SIZE / sizeof(uint32_t); i++) {
			uint32_t expected = i < WORDS ? pattern(n, i) : GUARD;

			if (src.map[i] != expected || dst.map[i] != expected)
				errx(1, "copy/guard mismatch iteration %u word %zu: "
				    "src=%#x dst=%#x expected=%#x", n, i,
				    src.map[i], dst.map[i], expected);
		}
	}
	close_handle(fd, src.handle);
	close_handle(fd, dst.handle);
	/* The UVM mappings must own their references after both handles close. */
	src.map[0] = GUARD;
	dst.map[0] = ~GUARD;
	check(src.map[0] == GUARD && dst.map[0] == ~GUARD,
	    "mapping lost storage after GEM_CLOSE");
	check(munmap(src.map, BO_SIZE) == 0 && munmap(dst.map, BO_SIZE) == 0,
	    "munmap GEM failed");
}

int
main(int argc, char **argv)
{
	const char *path;
	char name[32] = {0};
	struct drm_version version = { .name_len = sizeof(name) - 1, .name = name };
	struct drm_v3d_get_param param = { .param = DRM_V3D_PARAM_SUPPORTS_TFU };
	struct drm_v3d_get_bo_offset offset;
	struct drm_v3d_mmap_bo mapping;
	struct buffer bo;
	void *alien;
	pid_t children[2];
	int fd, other, status;
	unsigned int i;

	if (argc != 2)
		errx(1, "usage: %s /dev/dri/renderD128", argv[0]);
	path = argv[1];
	fd = open(path, O_RDWR);
	if (fd == -1)
		err(1, "open %s", path);
	if (ioctl(fd, DRM_IOCTL_VERSION, &version) == -1)
		err(1, "DRM_VERSION");
	check(strcmp(name, "v3d") == 0, "selected device is not V3D");
	if (ioctl(fd, DRM_IOCTL_V3D_GET_PARAM, &param) == -1)
		err(1, "GET_PARAM");
	check(param.value == 0, "bounded driver advertised general TFU support");
	printf("V3D %d.%d.%d: bounded native TFU acceptance\n",
	    version.version_major, version.version_minor, version.version_patchlevel);
	other = open(path, O_RDWR);
	if (other == -1)
		err(1, "second open");
	bo = allocate(fd);
	offset = (struct drm_v3d_get_bo_offset){ .handle = bo.handle };
	check(ioctl(other, DRM_IOCTL_V3D_GET_BO_OFFSET, &offset) == -1 &&
	    errno == ENOENT, "foreign GEM handle resolved");
	mapping = (struct drm_v3d_mmap_bo){ .handle = bo.handle };
	check(ioctl(other, DRM_IOCTL_V3D_MMAP_BO, &mapping) == -1 &&
	    errno == ENOENT, "foreign GEM mmap offset resolved");
	alien = mmap(NULL, BO_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
	    other, (off_t)bo.offset);
	check(alien == MAP_FAILED, "foreign GEM mapping was authorized");
	close_handle(fd, bo.handle);
	bo.map[0] = GUARD;
	check(bo.map[0] == GUARD && munmap(bo.map, BO_SIZE) == 0,
	    "mapped BO did not survive handle close");
	close(other);
	copy_test(fd, 260); /* Includes conversion-counter wrap. */
	close(fd);
	fflush(stdout);
	for (i = 0; i < 2; i++) {
		children[i] = fork();
		if (children[i] == -1)
			err(1, "fork");
		if (children[i] == 0) {
			fd = open(path, O_RDWR);
			if (fd == -1)
				err(1, "child open");
			copy_test(fd, 64);
			close(fd);
			_exit(0);
		}
	}
	for (i = 0; i < 2; i++) {
		check(waitpid(children[i], &status, 0) == children[i], "waitpid failed");
		check(WIFEXITED(status) && WEXITSTATUS(status) == 0,
		    "concurrent submitter failed");
	}
	puts("PASS: GEM ownership, cross-file denial, 388 TFU copies, guards, "
	    "counter wrap and concurrent clients");
	return 0;
}
