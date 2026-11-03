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
 * Firmware boot wait reference: Linux PowerVR driver,
 * 7b63ef2d55f24519e7e9e5f4d15dbea03f126e40, MIT license option.
 * Native DMA handoff: EmberBSD, AI-assisted. See a733-gpu-fwboot.md.
 */

#ifndef _SUN60I_A733_GPU_FWBOOT_H
#define _SUN60I_A733_GPU_FWBOOT_H

#include "sun60i_a733_gpu_fwmem.h"
#include "sun60i_a733_gpu_fwstart.h"
#include "../../../dev/powervr/pvr_fwboot.h"

#define A733_GPU_FWBOOT_MAX_POLLS PVR_FWBOOT_MAX_POLLS
#define A733_GPU_FWBOOT_POLL_US PVR_FWBOOT_POLL_US
#define A733_GPU_FWBOOT_TIMEOUT_US PVR_FWBOOT_TIMEOUT_US
#define sun60i_a733_gpu_fwboot_result pvr_fwboot_result

int sun60i_a733_gpu_fwboot(bus_space_tag_t, bus_space_handle_t,
    struct sun60i_a733_gpu_fwmem *, struct sun60i_a733_gpu_fwboot_result *);


#endif
