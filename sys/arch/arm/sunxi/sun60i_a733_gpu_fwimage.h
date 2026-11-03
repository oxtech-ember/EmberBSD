/* SPDX-License-Identifier: MIT */
/* Origin: EmberBSD PowerVR shared mechanism, from A733; AI-assisted. */
/*
 * Copyright (c) 2024 Imagination Technologies Ltd.
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
 * ELF loading reference: Linux PowerVR pvr_fw_util.c,
 * 7b63ef2d55f24519e7e9e5f4d15dbea03f126e40, MIT license option.
 * Native bounded image preparation: EmberBSD, AI-assisted.
 * See a733-gpu-fwimage.md.
 */

#ifndef _SUN60I_A733_GPU_FWIMAGE_H
#define _SUN60I_A733_GPU_FWIMAGE_H

#include "sun60i_a733_gpu_fwinfo.h"
#include "../../../dev/powervr/pvr_fwimage.h"

#define A733_GPU_FWIMAGE_MAX_PHDRS PVR_FWIMAGE_MAX_PHDRS
#define A733_GPU_FWIMAGE_SECTIONS PVR_FWIMAGE_SECTIONS
#define A733_GPU_FW_MIPS_CODE PVR_FW_MIPS_CODE
#define sun60i_a733_gpu_fw_layout pvr_fw_layout
#define sun60i_a733_gpu_fwimage pvr_fwimage
#define sun60i_a733_gpu_fwinfo pvr_fwinfo

int sun60i_a733_gpu_fwimage_prepare(const void *, size_t, uint64_t,
    void *, size_t, void *, size_t, struct sun60i_a733_gpu_fwimage *);


#endif /* _SUN60I_A733_GPU_FWIMAGE_H */
