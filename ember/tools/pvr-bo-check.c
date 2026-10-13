/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Check native PowerVR BO ownership and CPU mappings on a render node. */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pvr_drm.h"

static unsigned int checks;

static void
check(bool ok, const char *what)
{

	checks++;
	if (!ok) {
		fprintf(stderr, "FAIL: %s (errno %d: %s)\n", what, errno,
		    strerror(errno));
		exit(EXIT_FAILURE);
	}
}

static void
close_bo(int fd, uint32_t handle)
{
	struct drm_gem_close close_args = { .handle = handle };

	check(ioctl(fd, DRM_IOCTL_GEM_CLOSE, &close_args) == 0,
	    "close GEM handle");
}

static uint64_t
mmap_offset(int fd, uint32_t handle)
{
	struct drm_pvr_ioctl_get_bo_mmap_offset_args args = { .handle = handle };

	check(ioctl(fd, DRM_IOCTL_PVR_GET_BO_MMAP_OFFSET, &args) == 0,
	    "get mmap offset");
	check(args.offset != 0 && args.offset <= INT64_MAX, "valid mmap offset");
	return args.offset;
}

static void
reject_create(int fd, uint64_t size, uint64_t flags, uint32_t padding)
{
	struct drm_pvr_ioctl_create_bo_args args = {
		.size = size, .flags = flags, ._padding_c = padding,
	};
	int result;

	errno = 0;
	result = ioctl(fd, DRM_IOCTL_PVR_CREATE_BO, &args);
	check(result == -1 && errno == EINVAL, "reject invalid CREATE_BO");
}

int
main(int argc, char **argv)
{
	struct drm_version version = { 0 };
	struct drm_pvr_ioctl_create_bo_args create;
	struct drm_pvr_ioctl_get_bo_mmap_offset_args get;
	volatile uint32_t *full, *short_map;
	uint8_t *bytes;
	uint64_t offset, flags;
	size_t page, size, i;
	long native_page;
	char name[32] = { 0 };
	int fd, other, result;
	void *map;

	if (argc != 2) {
		fprintf(stderr, "usage: %s /dev/dri/renderD128\n", argv[0]);
		return EXIT_FAILURE;
	}
	native_page = sysconf(_SC_PAGESIZE);
	check(native_page >= 4096 &&
	    ((unsigned long)native_page & ((unsigned long)native_page - 1)) == 0,
	    "native page size");
	page = (size_t)native_page;
	size = 8U * 1024 * 1024;
	check(page < size && size % page == 0, "8 MiB mapping fits native pages");
	fd = open(argv[1], O_RDWR);
	check(fd >= 0, "open render node");
	other = open(argv[1], O_RDWR);
	check(other >= 0, "open independent render fd");
	version.name = name;
	version.name_len = sizeof(name) - 1;
	check(ioctl(fd, DRM_IOCTL_VERSION, &version) == 0, "query driver name");
	check(strcmp(name, "powervr") == 0, "PowerVR driver");

	reject_create(fd, 0, 0, 0);
	reject_create(fd, 1, 0, 0);
	reject_create(fd, 4095, 0, 0);
	reject_create(fd, UINT64_MAX, 0, 0);
	reject_create(fd, page, UINT64_C(1) << 63, 0);
	reject_create(fd, page, 0, 1);
	reject_create(fd, page, DRM_PVR_BO_PM_FW_PROTECT |
	    DRM_PVR_BO_ALLOW_CPU_USERSPACE_ACCESS, 0);

	/* All six supported flag combinations, including protected BOs. */
	for (flags = 0; flags < 6; flags++) {
		memset(&create, 0, sizeof(create));
		create.size = page;
		create.flags = flags;
		check(ioctl(fd, DRM_IOCTL_PVR_CREATE_BO, &create) == 0,
		    "create valid BO");
		offset = mmap_offset(fd, create.handle);
		errno = 0;
		map = mmap(NULL, page, PROT_READ | PROT_WRITE, MAP_SHARED,
		    fd, (off_t)offset);
		if ((flags & DRM_PVR_BO_ALLOW_CPU_USERSPACE_ACCESS) == 0) {
			check(map == MAP_FAILED && errno == EINVAL,
			    "deny CPU access without ALLOW_CPU");
		} else {
			check(map != MAP_FAILED, "map CPU-accessible BO");
			bytes = map;
			for (i = 0; i < page; i++)
				check(bytes[i] == 0, "new BO is zero");
			check(munmap(map, page) == 0, "unmap flag-test BO");
		}
		close_bo(fd, create.handle);
	}

	memset(&create, 0, sizeof(create));
	create.size = size;
	create.flags = DRM_PVR_BO_ALLOW_CPU_USERSPACE_ACCESS;
	check(ioctl(fd, DRM_IOCTL_PVR_CREATE_BO, &create) == 0,
	    "create lifetime-test BO");
	memset(&get, 0, sizeof(get));
	get.handle = create.handle;
	get._padding_4 = 1;
	errno = 0;
	result = ioctl(fd, DRM_IOCTL_PVR_GET_BO_MMAP_OFFSET, &get);
	check(result == -1 && errno == EINVAL, "reject mmap-offset padding");
	get._padding_4 = 0;
	errno = 0;
	result = ioctl(other, DRM_IOCTL_PVR_GET_BO_MMAP_OFFSET, &get);
	check(result == -1 && errno == ENOENT, "reject foreign GEM handle");
	offset = mmap_offset(fd, create.handle);
	check(mmap_offset(fd, create.handle) == offset, "stable mmap offset");
	errno = 0;
	map = mmap(NULL, page, PROT_READ | PROT_WRITE, MAP_SHARED,
	    other, (off_t)offset);
	check(map == MAP_FAILED && errno == EACCES, "deny foreign fd mapping");
	errno = 0;
	map = mmap(NULL, size + page, PROT_READ | PROT_WRITE, MAP_SHARED,
	    fd, (off_t)offset);
	check(map == MAP_FAILED && errno == EINVAL, "reject oversized mapping");
	full = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED,
	    fd, (off_t)offset);
	check(full != MAP_FAILED, "map complete BO");
	short_map = mmap(NULL, page, PROT_READ | PROT_WRITE, MAP_SHARED,
	    fd, (off_t)offset);
	check(short_map != MAP_FAILED, "map short BO prefix");
	for (i = 0; i < size / sizeof(*full); i++)
		check(full[i] == 0, "lifetime-test BO is zero");
	full[0] = UINT32_C(0x13579bdf);
	check(short_map[0] == UINT32_C(0x13579bdf), "shared CPU mappings");
	close_bo(fd, create.handle);
	errno = 0;
	result = ioctl(fd, DRM_IOCTL_PVR_GET_BO_MMAP_OFFSET, &get);
	check(result == -1 && errno == ENOENT, "closed handle is unavailable");
	check(close(fd) == 0, "close owner fd with live mappings");
	check(close(other) == 0, "close foreign fd");
	short_map[0] = UINT32_C(0x2468ace0);
	check(full[0] == UINT32_C(0x2468ace0), "mapping survives handle and fd");
	full[size / sizeof(*full) - 1] = UINT32_C(0xdeadbeef);
	check(full[size / sizeof(*full) - 1] == UINT32_C(0xdeadbeef),
	    "last mapped word remains writable");
	check(munmap((void *)(uintptr_t)full, size) == 0, "unmap complete BO");
	short_map[0] = UINT32_C(0xabcdef01);
	check(short_map[0] == UINT32_C(0xabcdef01), "last mapping remains live");
	check(munmap((void *)(uintptr_t)short_map, page) == 0,
	    "release final BO mapping");
	printf("PASS: %u native PowerVR BO checks (CPU mappings only)\n", checks);
	return EXIT_SUCCESS;
}
