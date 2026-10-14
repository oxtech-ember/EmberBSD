/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */
/*
 * Origin: Linux 7b63ef2d55f24519e7e9e5f4d15dbea03f126e40,
 * include/uapi/drm/pvr_drm.h. This subset selects the MIT alternative.
 * EmberBSD retains the query, buffer-object and GPU VM ABI below.
 * Unsupported job types return EOPNOTSUPP. They do not submit firmware work.
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

#define DRM_IOCTL_PVR_CREATE_CONTEXT \
	DRM_IOWR(DRM_COMMAND_BASE + 0x07, struct drm_pvr_ioctl_create_context_args)
#define DRM_IOCTL_PVR_DESTROY_CONTEXT \
	DRM_IOW(DRM_COMMAND_BASE + 0x08, struct drm_pvr_ioctl_destroy_context_args)
#define DRM_IOCTL_PVR_CREATE_FREE_LIST \
	DRM_IOWR(DRM_COMMAND_BASE + 0x09, struct drm_pvr_ioctl_create_free_list_args)
#define DRM_IOCTL_PVR_DESTROY_FREE_LIST \
	DRM_IOW(DRM_COMMAND_BASE + 0x0a, struct drm_pvr_ioctl_destroy_free_list_args)
#define DRM_IOCTL_PVR_CREATE_HWRT_DATASET \
	DRM_IOWR(DRM_COMMAND_BASE + 0x0b, \
	struct drm_pvr_ioctl_create_hwrt_dataset_args)
#define DRM_IOCTL_PVR_DESTROY_HWRT_DATASET \
	DRM_IOW(DRM_COMMAND_BASE + 0x0c, \
	struct drm_pvr_ioctl_destroy_hwrt_dataset_args)

/* Pinned upstream layout; local names without the ioctl_ middle word. */
struct drm_pvr_create_hwrt_geom_data_args {
	__u64 tpc_dev_addr;
	__u32 tpc_size;
	__u32 tpc_stride;
	__u64 vheap_table_dev_addr;
	__u64 rtc_dev_addr;
};

struct drm_pvr_create_hwrt_rt_data_args {
	__u64 pm_mlist_dev_addr;
	__u64 macrotile_array_dev_addr;
	__u64 region_header_dev_addr;
};

#define DRM_PVR_HWRT_FREE_LIST_LOCAL 0
#define DRM_PVR_HWRT_FREE_LIST_GLOBAL 1U

struct drm_pvr_ioctl_create_hwrt_dataset_args {
	struct drm_pvr_create_hwrt_geom_data_args geom_data_args;
	struct drm_pvr_create_hwrt_rt_data_args rt_data_args[2];
	__u32 free_list_handles[2];
	__u32 width;
	__u32 height;
	__u32 samples;
	__u32 layers;
	__u32 isp_merge_lower_x;
	__u32 isp_merge_lower_y;
	__u32 isp_merge_scale_x;
	__u32 isp_merge_scale_y;
	__u32 isp_merge_upper_x;
	__u32 isp_merge_upper_y;
	__u32 region_header_size;
	__u32 handle;
};

struct drm_pvr_ioctl_destroy_hwrt_dataset_args {
	__u32 handle;
	__u32 _padding_4;
};

#define DRM_IOCTL_PVR_SUBMIT_JOBS \
	DRM_IOWR(DRM_COMMAND_BASE + 0x0d, struct drm_pvr_ioctl_submit_jobs_args)

enum drm_pvr_ctx_priority {

	DRM_PVR_CTX_PRIORITY_LOW = -512,

	DRM_PVR_CTX_PRIORITY_NORMAL = 0,

	DRM_PVR_CTX_PRIORITY_HIGH = 512,
};

enum drm_pvr_ctx_type {

	DRM_PVR_CTX_TYPE_RENDER = 0,

	DRM_PVR_CTX_TYPE_COMPUTE,

	DRM_PVR_CTX_TYPE_TRANSFER_FRAG,
};

struct drm_pvr_ioctl_create_context_args {

	__u32 type;

	__u32 flags;

	__s32 priority;

	__u32 handle;

	__u64 static_context_state;

	__u32 static_context_state_len;

	__u32 vm_context_handle;

	__u64 callstack_addr;
};

struct drm_pvr_ioctl_destroy_context_args {

	__u32 handle;

	__u32 _padding_4;
};

struct drm_pvr_ioctl_create_free_list_args {

	__u64 free_list_gpu_addr;

	__u32 initial_num_pages;

	__u32 max_num_pages;

	__u32 grow_num_pages;

	__u32 grow_threshold;

	__u32 vm_context_handle;

	__u32 handle;
};

struct drm_pvr_ioctl_destroy_free_list_args {

	__u32 handle;

	__u32 _padding_4;
};


#define DRM_PVR_SYNC_OP_FLAG_HANDLE_TYPE_MASK 0xf
#define DRM_PVR_SYNC_OP_FLAG_HANDLE_TYPE_SYNCOBJ 0
#define DRM_PVR_SYNC_OP_FLAG_HANDLE_TYPE_TIMELINE_SYNCOBJ 1
#define DRM_PVR_SYNC_OP_FLAG_SIGNAL (1ULL << 31)
#define DRM_PVR_SYNC_OP_FLAG_WAIT 0

#define DRM_PVR_SYNC_OP_FLAGS_MASK (DRM_PVR_SYNC_OP_FLAG_HANDLE_TYPE_MASK | \
				    DRM_PVR_SYNC_OP_FLAG_SIGNAL)

struct drm_pvr_sync_op {

	__u32 handle;

	__u32 flags;

	__u64 value;
};

#define DRM_PVR_SUBMIT_JOB_GEOM_CMD_FIRST (1ULL << 0)
#define DRM_PVR_SUBMIT_JOB_GEOM_CMD_LAST (1ULL << 1)
#define DRM_PVR_SUBMIT_JOB_GEOM_CMD_SINGLE_CORE (1ULL << 2)
#define DRM_PVR_SUBMIT_JOB_GEOM_CMD_FLAGS_MASK                                 \
	(DRM_PVR_SUBMIT_JOB_GEOM_CMD_FIRST |                                   \
	 DRM_PVR_SUBMIT_JOB_GEOM_CMD_LAST |                                    \
	 DRM_PVR_SUBMIT_JOB_GEOM_CMD_SINGLE_CORE)

#define DRM_PVR_SUBMIT_JOB_FRAG_CMD_SINGLE_CORE (1ULL << 0)
#define DRM_PVR_SUBMIT_JOB_FRAG_CMD_DEPTHBUFFER (1ULL << 1)
#define DRM_PVR_SUBMIT_JOB_FRAG_CMD_STENCILBUFFER (1ULL << 2)
#define DRM_PVR_SUBMIT_JOB_FRAG_CMD_PREVENT_CDM_OVERLAP (1ULL << 3)
#define DRM_PVR_SUBMIT_JOB_FRAG_CMD_SCRATCHBUFFER (1ULL << 4)
#define DRM_PVR_SUBMIT_JOB_FRAG_CMD_GET_VIS_RESULTS (1ULL << 5)
#define DRM_PVR_SUBMIT_JOB_FRAG_CMD_PARTIAL_RENDER (1ULL << 6)
#define DRM_PVR_SUBMIT_JOB_FRAG_CMD_DISABLE_PIXELMERGE (1ULL << 7)
#define DRM_PVR_SUBMIT_JOB_FRAG_CMD_FLAGS_MASK                                 \
	(DRM_PVR_SUBMIT_JOB_FRAG_CMD_SINGLE_CORE |                             \
	 DRM_PVR_SUBMIT_JOB_FRAG_CMD_DEPTHBUFFER |                             \
	 DRM_PVR_SUBMIT_JOB_FRAG_CMD_STENCILBUFFER |                           \
	 DRM_PVR_SUBMIT_JOB_FRAG_CMD_PREVENT_CDM_OVERLAP |                     \
	 DRM_PVR_SUBMIT_JOB_FRAG_CMD_SCRATCHBUFFER |                           \
	 DRM_PVR_SUBMIT_JOB_FRAG_CMD_GET_VIS_RESULTS |                         \
	 DRM_PVR_SUBMIT_JOB_FRAG_CMD_PARTIAL_RENDER |                          \
	 DRM_PVR_SUBMIT_JOB_FRAG_CMD_DISABLE_PIXELMERGE)

#define DRM_PVR_SUBMIT_JOB_COMPUTE_CMD_PREVENT_ALL_OVERLAP (1ULL << 0)
#define DRM_PVR_SUBMIT_JOB_COMPUTE_CMD_SINGLE_CORE (1ULL << 1)
#define DRM_PVR_SUBMIT_JOB_COMPUTE_CMD_FLAGS_MASK         \
	(DRM_PVR_SUBMIT_JOB_COMPUTE_CMD_PREVENT_ALL_OVERLAP | \
	 DRM_PVR_SUBMIT_JOB_COMPUTE_CMD_SINGLE_CORE)

#define DRM_PVR_SUBMIT_JOB_TRANSFER_CMD_SINGLE_CORE (1ULL << 0)

#define DRM_PVR_SUBMIT_JOB_TRANSFER_CMD_FLAGS_MASK \
	DRM_PVR_SUBMIT_JOB_TRANSFER_CMD_SINGLE_CORE

enum drm_pvr_job_type {

	DRM_PVR_JOB_TYPE_GEOMETRY = 0,

	DRM_PVR_JOB_TYPE_FRAGMENT,

	DRM_PVR_JOB_TYPE_COMPUTE,

	DRM_PVR_JOB_TYPE_TRANSFER_FRAG,
};

struct drm_pvr_hwrt_data_ref {

	__u32 set_handle;

	__u32 data_index;
};

struct drm_pvr_job {

	__u32 type;

	__u32 context_handle;

	__u32 flags;

	__u32 cmd_stream_len;

	__u64 cmd_stream;

	struct drm_pvr_obj_array sync_ops;

	struct drm_pvr_hwrt_data_ref hwrt;
};

struct drm_pvr_ioctl_submit_jobs_args {

	struct drm_pvr_obj_array jobs;
};


#endif /* _PVR_DRM_UAPI_H_ */
