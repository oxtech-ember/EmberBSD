/* SPDX-License-Identifier: MIT */
/* Origin: EmberBSD PowerVR adapter; MIT-option Linux reference, AI-assisted. */
/*
 * Copyright (c) 2023 Imagination Technologies Ltd.
 * Copyright (c) 2026 EmberBSD contributors.
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
 *
 * Firmware initialization rules: Linux PowerVR driver,
 * 7b63ef2d55f24519e7e9e5f4d15dbea03f126e40, MIT license option.
 * Native CPU initializer: EmberBSD, AI-assisted. See a733-gpu-fwinit.md.
 */

#ifndef _SUN60I_A733_GPU_FWINIT_H
#define _SUN60I_A733_GPU_FWINIT_H

#include "../../../dev/powervr/pvr_fwinit.h"

#define A733_GPU_FWINIT_CONFIG PVR_FWINIT_CONFIG
#define A733_GPU_FWINIT_CONFIG_SIZE PVR_FWINIT_CONFIG_SIZE
#define A733_GPU_FWINIT_CONNECTION PVR_FWINIT_CONNECTION
#define A733_GPU_FWINIT_CONNECTION_VA PVR_FWINIT_CONNECTION_VA
#define A733_GPU_FWINIT_FAULT PVR_FWINIT_FAULT
#define A733_GPU_FWINIT_FWCCB_CMDS PVR_FWINIT_FWCCB_CMDS
#define A733_GPU_FWINIT_FWCCB_COUNT PVR_FWINIT_FWCCB_COUNT
#define A733_GPU_FWINIT_FWCCB_CTL PVR_FWINIT_FWCCB_CTL
#define A733_GPU_FWINIT_GPU_UTIL PVR_FWINIT_GPU_UTIL
#define A733_GPU_FWINIT_HWR PVR_FWINIT_HWR
#define A733_GPU_FWINIT_KCCB_CMDS PVR_FWINIT_KCCB_CMDS
#define A733_GPU_FWINIT_KCCB_COUNT PVR_FWINIT_KCCB_COUNT
#define A733_GPU_FWINIT_KCCB_CTL PVR_FWINIT_KCCB_CTL
#define A733_GPU_FWINIT_KCCB_RTN PVR_FWINIT_KCCB_RTN
#define A733_GPU_FWINIT_MAX_WORK_SIZE PVR_FWINIT_MAX_WORK_SIZE
#define A733_GPU_FWINIT_MMUCACHE_SYNC PVR_FWINIT_MMUCACHE_SYNC
#define A733_GPU_FWINIT_OBJECT_COUNT PVR_FWINIT_OBJECT_COUNT
#define A733_GPU_FWINIT_OSDATA PVR_FWINIT_OSDATA
#define A733_GPU_FWINIT_OSINIT PVR_FWINIT_OSINIT
#define A733_GPU_FWINIT_OSINIT_VA PVR_FWINIT_OSINIT_VA
#define A733_GPU_FWINIT_PAGE_SIZE PVR_FWINIT_PAGE_SIZE
#define A733_GPU_FWINIT_POWER_SYNC PVR_FWINIT_POWER_SYNC
#define A733_GPU_FWINIT_RUNTIME_CFG PVR_FWINIT_RUNTIME_CFG
#define A733_GPU_FWINIT_SYSDATA PVR_FWINIT_SYSDATA
#define A733_GPU_FWINIT_SYSINIT PVR_FWINIT_SYSINIT
#define A733_GPU_FWINIT_SYSINIT_VA PVR_FWINIT_SYSINIT_VA
#define A733_GPU_FWINIT_TRACE0 PVR_FWINIT_TRACE0
#define A733_GPU_FWINIT_TRACE1 PVR_FWINIT_TRACE1
#define A733_GPU_FWINIT_TRACE_CTL PVR_FWINIT_TRACE_CTL
#define A733_GPU_FWINIT_WORK PVR_FWINIT_WORK
#define A733_GPU_FWINIT_WORK_VA PVR_FWINIT_WORK_VA
#define sun60i_a733_gpu_fwinit_buffer pvr_fwinit_buffer
#define sun60i_a733_gpu_fwinit_layout pvr_fwinit_layout
#define sun60i_a733_gpu_fwinit_object pvr_fwinit_object
#define sun60i_a733_gpu_fwinit_region pvr_fwinit_region

size_t sun60i_a733_gpu_fwinit_work_size(void);

/*
 * Initialize CPU-owned buffers. No allocation, DMA, MMIO, queue submission,
 * or firmware execution. Use page-aligned CPU buffers and work_phys. Work
 * capacity must be a page multiple, at least the required size, and at most
 * 256 KiB. Config size must be exactly three pages. Only the required work
 * prefix is modified; spare capacity remains unchanged. Config is cleared
 * in full. All three output spans must be disjoint and nonwrapping.
 *
 * The caller serializes access, owns the RAM, and supplies verified physical
 * addresses and hardware values. The entire claimed work physical span must
 * fit 36 bits and avoid 0x1fc00000..0x1fc03fff. Config pages will be mapped
 * separately at the three declared firmware VAs; they are not contiguous VAs.
 * core_hz must fit a nonzero uint32_t. clusters and slc_kib must be nonzero.
 * out must have its native alignment. The result retains no CPU pointers.
 *
 * Return positive errno; preserve both buffers and out on every error.
 * EINVAL: invalid argument, size, alignment, range, or overlapping spans.
 * EOPNOTSUPP: physical work span intersects the wrapper remap window.
 */
int sun60i_a733_gpu_fwinit(void *, size_t, void *, size_t, uint64_t,
    uint64_t, uint32_t, uint32_t, struct sun60i_a733_gpu_fwinit_layout *);


#endif /* _SUN60I_A733_GPU_FWINIT_H */
