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

#include "sun60i_a733_gpu_fwinit.h"

size_t
sun60i_a733_gpu_fwinit_work_size(void)
{
	return pvr_fwinit_work_size();
}

int
sun60i_a733_gpu_fwinit(void *work, size_t work_size, void *config,
    size_t config_size, uint64_t work_pa, uint64_t clock_hz,
    uint32_t clusters, uint32_t slc_kb,
    struct sun60i_a733_gpu_fwinit_layout *out)
{
	return pvr_fwinit(work, work_size, config, config_size, work_pa,
	    clock_hz, clusters, slc_kb, out);
}
