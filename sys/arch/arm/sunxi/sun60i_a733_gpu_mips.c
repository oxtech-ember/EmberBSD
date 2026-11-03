/* SPDX-License-Identifier: MIT */
/* Origin: EmberBSD PowerVR shared mechanism, from A733; AI-assisted. */
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
 * Page table and boot data definitions: Linux PowerVR driver,
 * 7b63ef2d55f24519e7e9e5f4d15dbea03f126e40, MIT license option.
 * Native checked encoders: EmberBSD, AI-assisted. See a733-gpu-mips.md.
 */

#include "sun60i_a733_gpu_mips.h"
#ifdef _KERNEL
#include <sys/errno.h>
#else
#include <errno.h>
#endif

int
sun60i_a733_gpu_mips_map(void *table, size_t size, uint64_t va,
    const uint64_t *pages, size_t count)
{
	return pvr_mips_map(table, size, va, pages, count);
}

int
sun60i_a733_gpu_mips_unmap(void *table, size_t size, uint64_t va,
    const uint64_t *pages, size_t count)
{
	return pvr_mips_unmap(table, size, va, pages, count);
}

int
sun60i_a733_gpu_mips_boot_data(void *out, size_t size, uint64_t stack,
    uint64_t regs, const uint64_t pages[A733_GPU_MIPS_PT_PAGES])
{
	if (regs != A733_GPU_MIPS_REGISTER_PA)
		return EINVAL;
	return pvr_mips_boot_data(out, size, stack, regs, pages);
}
