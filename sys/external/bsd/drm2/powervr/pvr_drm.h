/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */
/*
 * Origin: Linux 7b63ef2d55f24519e7e9e5f4d15dbea03f126e40,
 * include/uapi/drm/pvr_drm.h. This subset selects the MIT alternative.
 * EmberBSD retains the query, buffer-object and GPU VM ABI below.
 * Execution contexts and submission are not implemented.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#ifndef _PVR_DRM_UAPI_H_
#define _PVR_DRM_UAPI_H_

#include <drm/drm.h>

#define DRM_IOCTL_PVR_DEV_QUERY \
	DRM_IOWR(DRM_COMMAND_BASE + 0x00, struct drm_pvr_ioctl_dev_query_args)
#define DRM_IOCTL_PVR_CREATE_BO \
	DRM_IOWR(DRM_COMMAND_BASE + 0x01, struct drm_pvr_ioctl_create_bo_args)
#define DRM_IOCTL_PVR_GET_BO_MMAP_OFFSET \
	DRM_IOWR(DRM_COMMAND_BASE + 0x02, struct drm_pvr_ioctl_get_bo_mmap_offset_args)
#define DRM_IOCTL_PVR_CREATE_VM_CONTEXT \
	DRM_IOWR(DRM_COMMAND_BASE + 0x03, struct drm_pvr_ioctl_create_vm_context_args)
#define DRM_IOCTL_PVR_DESTROY_VM_CONTEXT \
	DRM_IOW(DRM_COMMAND_BASE + 0x04, struct drm_pvr_ioctl_destroy_vm_context_args)
#define DRM_IOCTL_PVR_VM_MAP \
	DRM_IOW(DRM_COMMAND_BASE + 0x05, struct drm_pvr_ioctl_vm_map_args)
#define DRM_IOCTL_PVR_VM_UNMAP \
	DRM_IOW(DRM_COMMAND_BASE + 0x06, struct drm_pvr_ioctl_vm_unmap_args)

enum drm_pvr_dev_query {
	DRM_PVR_DEV_QUERY_GPU_INFO_GET = 0,
	DRM_PVR_DEV_QUERY_RUNTIME_INFO_GET,
	DRM_PVR_DEV_QUERY_QUIRKS_GET,
	DRM_PVR_DEV_QUERY_ENHANCEMENTS_GET,
	DRM_PVR_DEV_QUERY_HEAP_INFO_GET,
	DRM_PVR_DEV_QUERY_STATIC_DATA_AREAS_GET,
};

struct drm_pvr_dev_query_gpu_info {
	__u64 gpu_id;
	__u32 num_phantoms;
	__u32 _padding_c;
};

struct drm_pvr_dev_query_runtime_info {
	__u64 free_list_min_pages;
	__u64 free_list_max_pages;
	__u32 common_store_alloc_region_size;
	__u32 common_store_partition_space_size;
	__u32 max_coeffs;
	__u32 cdm_max_local_mem_size_regs;
};

struct drm_pvr_dev_query_quirks {
	__u64 quirks;
	__u16 count;
	__u16 musthave_count;
	__u32 _padding_c;
};

struct drm_pvr_dev_query_enhancements {
	__u64 enhancements;
	__u16 count;
	__u16 _padding_a;
	__u32 _padding_c;
};

struct drm_pvr_ioctl_dev_query_args {
	__u32 type;
	__u32 size;
	__u64 pointer;
};

struct drm_pvr_obj_array {
	__u32 stride;
	__u32 count;
	__u64 array;
};

enum drm_pvr_heap_id {
	DRM_PVR_HEAP_GENERAL = 0,
	DRM_PVR_HEAP_PDS_CODE_DATA,
	DRM_PVR_HEAP_USC_CODE,
	DRM_PVR_HEAP_RGNHDR,
	DRM_PVR_HEAP_VIS_TEST,
	DRM_PVR_HEAP_TRANSFER_FRAG,
	DRM_PVR_HEAP_COUNT
};

struct drm_pvr_heap {
	__u64 base;
	__u64 size;
	__u32 flags;
	__u32 page_size_log2;
};

struct drm_pvr_dev_query_heap_info {
	struct drm_pvr_obj_array heaps;
};

enum drm_pvr_static_data_area_usage {
	DRM_PVR_STATIC_DATA_AREA_EOT = 0,
	DRM_PVR_STATIC_DATA_AREA_FENCE,
	DRM_PVR_STATIC_DATA_AREA_VDM_SYNC,
	DRM_PVR_STATIC_DATA_AREA_YUV_CSC
};

struct drm_pvr_static_data_area {
	__u16 area_usage;
	__u16 location_heap_id;
	__u32 size;
	__u64 offset;
};

struct drm_pvr_dev_query_static_data_areas {
	struct drm_pvr_obj_array static_data_areas;
};

#define DRM_PVR_BO_BYPASS_DEVICE_CACHE		((__u64)1 << 0)
#define DRM_PVR_BO_PM_FW_PROTECT			((__u64)1 << 1)
#define DRM_PVR_BO_ALLOW_CPU_USERSPACE_ACCESS	((__u64)1 << 2)
#define DRM_PVR_BO_FLAGS_MASK	(DRM_PVR_BO_BYPASS_DEVICE_CACHE | \
	DRM_PVR_BO_PM_FW_PROTECT | DRM_PVR_BO_ALLOW_CPU_USERSPACE_ACCESS)

struct drm_pvr_ioctl_create_bo_args {
	__u64 size;
	__u32 handle;
	__u32 _padding_c;
	__u64 flags;
};

struct drm_pvr_ioctl_get_bo_mmap_offset_args {
	__u32 handle;
	__u32 _padding_4;
	__u64 offset;
};

struct drm_pvr_ioctl_create_vm_context_args {
	__u32 handle;
	__u32 _padding_4;
};

struct drm_pvr_ioctl_destroy_vm_context_args {
	__u32 handle;
	__u32 _padding_4;
};

struct drm_pvr_ioctl_vm_map_args {
	__u32 vm_context_handle;
	__u32 flags;
	__u64 device_addr;
	__u32 handle;
	__u32 _padding_14;
	__u64 offset;
	__u64 size;
};

struct drm_pvr_ioctl_vm_unmap_args {
	__u32 vm_context_handle;
	__u32 _padding_4;
	__u64 device_addr;
	__u64 size;
};

#endif /* _PVR_DRM_UAPI_H_ */
