/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */
/*
 * Origin: Linux 7b63ef2d55f24519e7e9e5f4d15dbea03f126e40,
 * include/uapi/drm/pvr_drm.h. This subset selects the MIT alternative.
 * EmberBSD retains the DEV_QUERY ABI. Other ioctls are not implemented.
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

#endif /* _PVR_DRM_UAPI_H_ */
