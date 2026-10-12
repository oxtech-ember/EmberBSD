/* SPDX-License-Identifier: MIT */
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
 * GPU page table definitions and encoders: Linux PowerVR driver,
 * 7b63ef2d55f24519e7e9e5f4d15dbea03f126e40, MIT license option.
 * Native checked encoders: EmberBSD, AI-assisted. See a733-gpu-mmu.md.
 */

#include "sun60i_a733_gpu_mmu.h"
#ifdef _KERNEL
#include <sys/errno.h>
#include <sys/null.h>
#else
#include <errno.h>
#endif

#define MMU_PTE_OPTIONS	(A733_GPU_MMU_PTE_READ_ONLY | \
    A733_GPU_MMU_PTE_COHERENT | A733_GPU_MMU_PTE_SLC_BYPASS | \
    A733_GPU_MMU_PTE_PM_FW_PROTECT)

static int
mmu_output_valid(const void *out, size_t size, size_t alignment)
{
	uintptr_t address = (uintptr_t)out;

	return out != NULL && address % alignment == 0 &&
	    size - 1 <= (uintptr_t)-1 - address;
}

static int
mmu_pa_valid(uint64_t pa)
{
	return pa >> A733_GPU_MMU_PA_BITS == 0 &&
	    (pa & (A733_GPU_MMU_PAGE_SIZE - 1)) == 0;
}

int
sun60i_a733_gpu_mmu_indices(uint64_t va,
    struct sun60i_a733_gpu_mmu_indices *out)
{
	struct sun60i_a733_gpu_mmu_indices indices;

	if (!mmu_output_valid(out, sizeof(*out),
	    _Alignof(struct sun60i_a733_gpu_mmu_indices)) ||
	    va >> A733_GPU_MMU_VA_BITS != 0)
		return EINVAL;
	indices.pc = (uint16_t)((va >> 30) & 0x3ff);
	indices.pd = (uint16_t)((va >> 21) & 0x1ff);
	indices.pt = (uint16_t)((va >> 12) & 0x1ff);
	indices.offset = (uint16_t)(va & 0xfff);
	*out = indices;
	return 0;
}

int
sun60i_a733_gpu_mmu_pce(uint64_t pd_pa, uint32_t *out)
{
	if (!mmu_output_valid(out, sizeof(*out), _Alignof(uint32_t)) ||
	    !mmu_pa_valid(pd_pa))
		return EINVAL;
	/* PCE bits 31:4 contain PD address bits 39:12. PA is limited to 36. */
	*out = (uint32_t)(pd_pa >> 8) | UINT32_C(1);
	return 0;
}

int
sun60i_a733_gpu_mmu_pde(uint64_t pt_pa, uint64_t *out)
{
	if (!mmu_output_valid(out, sizeof(*out), _Alignof(uint64_t)) ||
	    !mmu_pa_valid(pt_pa))
		return EINVAL;
	/* The 4 KiB page-size encoding is zero; pending is also clear. */
	*out = pt_pa | UINT64_C(1);
	return 0;
}

int
sun60i_a733_gpu_mmu_pte(uint64_t pa, uint64_t flags, uint64_t *out)
{
	if (!mmu_output_valid(out, sizeof(*out), _Alignof(uint64_t)) ||
	    !mmu_pa_valid(pa) || (flags & ~MMU_PTE_OPTIONS) != 0)
		return EINVAL;
	*out = pa | flags | UINT64_C(1);
	return 0;
}
