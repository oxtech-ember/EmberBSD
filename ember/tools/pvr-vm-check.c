/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Exercise native PowerVR VM ownership. This does not submit GPU work. */
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
reject(int fd, unsigned long request, void *args, int error, const char *what)
{

	errno = 0;
	check(ioctl(fd, request, args) == -1 && errno == error, what);
}

static uint32_t
create_vm(int fd)
{
	struct drm_pvr_ioctl_create_vm_context_args args = { 0 };

	check(ioctl(fd, DRM_IOCTL_PVR_CREATE_VM_CONTEXT, &args) == 0,
	    "create VM");
	check(args.handle != 0, "nonzero VM handle");
	return args.handle;
}

static void
destroy_vm(int fd, uint32_t handle)
{
	struct drm_pvr_ioctl_destroy_vm_context_args args = { .handle = handle };

	check(ioctl(fd, DRM_IOCTL_PVR_DESTROY_VM_CONTEXT, &args) == 0,
	    "destroy VM and retained mappings");
}

static void
unmap_vm(int fd, uint32_t handle, uint64_t address, uint64_t size)
{
	struct drm_pvr_ioctl_vm_unmap_args args = {
		.vm_context_handle = handle, .device_addr = address, .size = size,
	};

	check(ioctl(fd, DRM_IOCTL_PVR_VM_UNMAP, &args) == 0, "unmap exact range");
}

static void
query_array(int fd, uint32_t type, struct drm_pvr_obj_array *array)
{
	struct drm_pvr_ioctl_dev_query_args args = {
		.type = type, .size = sizeof(*array), .pointer = (uintptr_t)array,
	};

	check(ioctl(fd, DRM_IOCTL_PVR_DEV_QUERY, &args) == 0 &&
	    args.size == sizeof(*array), "query array");
}

int
main(int argc, char **argv)
{
	struct drm_pvr_heap heaps[DRM_PVR_HEAP_COUNT] = { 0 };
	struct drm_pvr_static_data_area areas[5] = { 0 };
	struct drm_pvr_obj_array array = { 0 };
	struct drm_pvr_ioctl_create_vm_context_args invalid_vm = { 0xdead, 1 };
	struct drm_pvr_ioctl_destroy_vm_context_args destroy;
	struct drm_pvr_ioctl_create_bo_args bo = {
		.size = 8U * 1024 * 1024,
		.flags = DRM_PVR_BO_ALLOW_CPU_USERSPACE_ACCESS,
	};
	struct drm_pvr_ioctl_get_bo_mmap_offset_args get = { 0 };
	struct drm_pvr_ioctl_vm_map_args map, good;
	struct drm_pvr_ioctl_vm_unmap_args unmap;
	struct drm_gem_close close_bo;
	volatile uint32_t *cpu;
	uint32_t vm, second, handles[128];
	uint64_t base, boundary;
	size_t i, active = 0;
	int fd, other;

	if (argc != 2) {
		fprintf(stderr, "usage: %s /dev/dri/renderD128\n", argv[0]);
		return EXIT_FAILURE;
	}
	check(sysconf(_SC_PAGESIZE) == 4096, "supported 4 KiB VM profile");
	fd = open(argv[1], O_RDWR);
	check(fd >= 0, "open render node");
	other = open(argv[1], O_RDWR);
	check(other >= 0, "open independent render file");
	query_array(fd, DRM_PVR_DEV_QUERY_HEAP_INFO_GET, &array);
	check(array.count == DRM_PVR_HEAP_COUNT && array.stride == sizeof(heaps[0]),
	    "heap discovery");
	array.array = (uintptr_t)heaps;
	query_array(fd, DRM_PVR_DEV_QUERY_HEAP_INFO_GET, &array);
	check(array.count == DRM_PVR_HEAP_COUNT && array.stride == sizeof(heaps[0]),
	    "complete heap output");
	for (i = 0; i < DRM_PVR_HEAP_COUNT; i++) {
		check(heaps[i].flags == 0 && heaps[i].page_size_log2 == 12,
		    "heap page size and flags");
		if (heaps[i].size != 0) active++;
	}
	check(active == 5 && heaps[DRM_PVR_HEAP_RGNHDR].size == 0,
	    "BXM profile omits BRN63142 heap");
	array = (struct drm_pvr_obj_array){ 0 };
	query_array(fd, DRM_PVR_DEV_QUERY_STATIC_DATA_AREAS_GET, &array);
	check(array.count == 5 && array.stride == sizeof(areas[0]),
	    "static area discovery");
	array.array = (uintptr_t)areas;
	query_array(fd, DRM_PVR_DEV_QUERY_STATIC_DATA_AREAS_GET, &array);
	check(array.count == 5 && array.stride == sizeof(areas[0]),
	    "complete static-area output");
	for (i = 0; i < 5; i++) {
		check(areas[i].location_heap_id < DRM_PVR_HEAP_COUNT,
		    "static area heap index");
		check(areas[i].size != 0 && areas[i].offset <
		    heaps[areas[i].location_heap_id].size && areas[i].size <=
		    heaps[areas[i].location_heap_id].size - areas[i].offset,
		    "static area lies within its heap");
	}
	reject(fd, DRM_IOCTL_PVR_CREATE_VM_CONTEXT, &invalid_vm, EINVAL,
	    "reject CREATE_VM padding");
	check(invalid_vm.handle == 0xdead, "failed CREATE_VM preserves output");
	vm = create_vm(fd);
	second = create_vm(fd);
	check(vm != second, "independent VM handles");
	destroy = (struct drm_pvr_ioctl_destroy_vm_context_args){ vm, 0 };
	reject(other, DRM_IOCTL_PVR_DESTROY_VM_CONTEXT, &destroy, EINVAL,
	    "foreign VM is unavailable");
	destroy.handle = UINT32_MAX;
	reject(fd, DRM_IOCTL_PVR_DESTROY_VM_CONTEXT, &destroy, EINVAL,
	    "reject out-of-range VM handle");
	check(ioctl(fd, DRM_IOCTL_PVR_CREATE_BO, &bo) == 0, "create 8 MiB BO");
	get.handle = bo.handle;
	check(ioctl(fd, DRM_IOCTL_PVR_GET_BO_MMAP_OFFSET, &get) == 0,
	    "get CPU mapping offset");
	cpu = mmap(NULL, bo.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, get.offset);
	check(cpu != MAP_FAILED, "map CPU buffer");
	cpu[0] = 0x13579bdf;
	base = heaps[DRM_PVR_HEAP_GENERAL].base;
	boundary = base + (UINT64_C(1) << 30) - 4096;
	good = (struct drm_pvr_ioctl_vm_map_args){
		.vm_context_handle = vm, .device_addr = boundary,
		.handle = bo.handle, .size = bo.size,
	};
	map = good;
	reject(other, DRM_IOCTL_PVR_VM_MAP, &map, ENOENT, "foreign BO is unavailable");
	map.flags = 1;
	reject(fd, DRM_IOCTL_PVR_VM_MAP, &map, EINVAL, "reject MAP flags");
	map = good; map._padding_14 = 1;
	reject(fd, DRM_IOCTL_PVR_VM_MAP, &map, EINVAL, "reject MAP padding");
	map = good; map.size = 0;
	reject(fd, DRM_IOCTL_PVR_VM_MAP, &map, EINVAL, "reject empty MAP");
	map = good; map.device_addr++;
	reject(fd, DRM_IOCTL_PVR_VM_MAP, &map, EINVAL, "reject unaligned VA");
	map = good; map.offset = 1;
	reject(fd, DRM_IOCTL_PVR_VM_MAP, &map, EINVAL, "reject unaligned BO offset");
	map = good; map.offset = 4096;
	reject(fd, DRM_IOCTL_PVR_VM_MAP, &map, EINVAL, "reject BO overrun");
	map = good; map.vm_context_handle = UINT32_MAX;
	reject(fd, DRM_IOCTL_PVR_VM_MAP, &map, EINVAL, "reject missing VM");
	map = good; map.device_addr = base + heaps[DRM_PVR_HEAP_GENERAL].size - 4096;
	reject(fd, DRM_IOCTL_PVR_VM_MAP, &map, EINVAL, "reject cross-heap range");
	map = good; map.device_addr = UINT64_MAX - 4095;
	reject(fd, DRM_IOCTL_PVR_VM_MAP, &map, EINVAL, "reject wrapping GPU range");
	map = good;
	check(ioctl(fd, DRM_IOCTL_PVR_VM_MAP, &map) == 0,
	    "map across 2 MiB and 1 GiB table boundaries");
	reject(fd, DRM_IOCTL_PVR_VM_MAP, &map, EEXIST,
	    "overlap replacement is explicitly unsupported");
	map.vm_context_handle = second;
	check(ioctl(fd, DRM_IOCTL_PVR_VM_MAP, &map) == 0, "same VA in another VM");
	unmap = (struct drm_pvr_ioctl_vm_unmap_args){ vm, 0, boundary, 4096 };
	reject(fd, DRM_IOCTL_PVR_VM_UNMAP, &unmap, ENOENT, "reject partial unmap");
	unmap.size = bo.size; unmap._padding_4 = 1;
	reject(fd, DRM_IOCTL_PVR_VM_UNMAP, &unmap, EINVAL, "reject UNMAP padding");
	/* More than five sparse windows, including every available heap. */
	for (i = 0; i < DRM_PVR_HEAP_COUNT; i++) {
		if (heaps[i].size == 0) continue;
		map = good; map.size = 4096; map.offset = 4096;
		map.device_addr = heaps[i].base;
		check(ioctl(fd, DRM_IOCTL_PVR_VM_MAP, &map) == 0, "map heap first page");
		map.device_addr += heaps[i].size - 4096;
		check(ioctl(fd, DRM_IOCTL_PVR_VM_MAP, &map) == 0, "map heap last page");
	}
	for (i = 0; i < 128; i++) {
		handles[i] = create_vm(fd);
		map = good; map.vm_context_handle = handles[i]; map.size = 4096;
		check(ioctl(fd, DRM_IOCTL_PVR_VM_MAP, &map) == 0, "map many independent VMs");
	}
	close_bo = (struct drm_gem_close){ .handle = bo.handle };
	check(ioctl(fd, DRM_IOCTL_GEM_CLOSE, &close_bo) == 0, "close mapped GEM handle");
	check(cpu[0] == 0x13579bdf, "CPU contents survive GEM_CLOSE");
	unmap_vm(fd, vm, boundary, bo.size);
	unmap_vm(fd, second, boundary, bo.size);
	for (i = 0; i < 128; i++) destroy_vm(fd, handles[i]);
	destroy_vm(fd, second);
	/* Leave ten mappings for postclose. The CPU mapping still owns the BO. */
	check(close(other) == 0 && close(fd) == 0, "close files with retained VM mappings");
	check(cpu[0] == 0x13579bdf, "CPU mapping survives VM postclose");
	cpu[0] = 0x2468ace0;
	check(cpu[0] == 0x2468ace0, "CPU mapping remains writable");
	check(munmap((void *)cpu, bo.size) == 0, "release final BO reference");
	printf("PASS: %u native PowerVR VM checks; CPU-owned tables only\n", checks);
	return EXIT_SUCCESS;
}
