/* SPDX-License-Identifier: MIT */
/* Origin: EmberBSD; strict A733 adapter to common compute serializer. */
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
 * Context and queue initialization: Linux PowerVR driver,
 * 7b63ef2d55f24519e7e9e5f4d15dbea03f126e40, MIT license option.
 * Native CPU serializer: EmberBSD, AI-assisted. See a733-gpu-fwcompute.md.
 */

#ifndef _SUN60I_A733_GPU_FWCOMPUTE_H
#define _SUN60I_A733_GPU_FWCOMPUTE_H
#include "../../../dev/powervr/pvr_fwcompute.h"
#define A733_GPU_FWCOMPUTE_CCCB_SIZE PVR_FWCOMPUTE_CCCB_SIZE
#define A733_GPU_FWCOMPUTE_USED_BYTES PVR_FWCOMPUTE_USED_BYTES
#define A733_GPU_FWCOMPUTE_CONTEXT PVR_FWCOMPUTE_CONTEXT
#define A733_GPU_FWCOMPUTE_CTL PVR_FWCOMPUTE_CTL
#define A733_GPU_FWCOMPUTE_CCCB PVR_FWCOMPUTE_CCCB
#define A733_GPU_FWCOMPUTE_STATE PVR_FWCOMPUTE_STATE
#define A733_GPU_FWCOMPUTE_TIMELINE PVR_FWCOMPUTE_TIMELINE
#define A733_GPU_FWCOMPUTE_VM_CONTEXT PVR_FWCOMPUTE_VM_CONTEXT
#define A733_GPU_FWCOMPUTE_OBJECTS PVR_FWCOMPUTE_OBJECTS
#define A733_GPU_FWCOMPUTE_USC PVR_FWCOMPUTE_USC
#define A733_GPU_FWCOMPUTE_PDS PVR_FWCOMPUTE_PDS
#define A733_GPU_FWCOMPUTE_STREAM PVR_FWCOMPUTE_STREAM
#define A733_GPU_FWCOMPUTE_PROGRAMS PVR_FWCOMPUTE_PROGRAMS
#define sun60i_a733_gpu_fwcompute_object pvr_fwcompute_object
#define sun60i_a733_gpu_fwcompute_program pvr_fwcompute_program
#define sun60i_a733_gpu_fwcompute_fwspan pvr_fwcompute_fwspan
#define sun60i_a733_gpu_fwcompute_program_span pvr_fwcompute_program_span
#define sun60i_a733_gpu_fwcompute_inputs pvr_fwcompute_inputs
#define sun60i_a733_gpu_fwcompute_image pvr_fwcompute_image
int sun60i_a733_gpu_fwcompute_prepare(const struct pvr_fwcompute_inputs *,
    struct pvr_fwcompute_image *, size_t);
#endif
