/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */
/*
 * Origin: Linux 7b63ef2d55f24519e7e9e5f4d15dbea03f126e40,
 * include/uapi/drm/pvr_drm.h. This subset selects the MIT alternative.
 * EmberBSD retains the DEV_QUERY and buffer-object ABI below.
 * VM, context and submission ioctls are not implemented.
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

struct drm_pvr_ioctl_dev_query_args {
	__u32 type;
	__u32 size;
	__u64 pointer;
};

/* Device cache and access restrictions are retained for future GPU mappings. */
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

#endif /* _PVR_DRM_UAPI_H_ */
