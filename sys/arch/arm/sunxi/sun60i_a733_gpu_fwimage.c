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

#include "sun60i_a733_gpu_fwimage.h"

int
sun60i_a733_gpu_fwimage_prepare(const void *bytes, size_t size, uint64_t bvnc,
    void *code, size_t code_size, void *data, size_t data_size,
    struct sun60i_a733_gpu_fwimage *out)
{
	return pvr_fwimage_prepare(bytes, size, bvnc, code, code_size,
	    data, data_size, out);
}
