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
 * Page table and boot data definitions: Linux PowerVR driver,
 * 7b63ef2d55f24519e7e9e5f4d15dbea03f126e40, MIT license option.
 * Native checked encoders: EmberBSD, AI-assisted. See a733-gpu-mips.md.
 */

#include "sun60i_a733_gpu_mips.h"
#ifdef _KERNEL
#include <sys/errno.h>
#include <sys/null.h>
#else
#include <errno.h>
#endif

#define MIPS_PTE_COUNT	(A733_GPU_MIPS_PT_SIZE / 4)
#define MIPS_PFN_MASK	UINT32_C(0x3fffffc0)
#define MIPS_PTE_FLAGS	UINT32_C(0x17) /* Uncached, dirty, valid, global. */

static uint32_t
mips_get32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
	    (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void
mips_put32(uint8_t *p, uint32_t value)
{
	unsigned int i;

	for (i = 0; i < 4; i++)
		p[i] = (uint8_t)(value >> (i * 8));
}

static void
mips_put64(uint8_t *p, uint64_t value)
{
	unsigned int i;

	for (i = 0; i < 8; i++)
		p[i] = (uint8_t)(value >> (i * 8));
}

static int
mips_pa_check(uint64_t pa)
{
	if ((pa & (A733_GPU_MIPS_PAGE_SIZE - 1)) != 0 || pa >> 36 != 0)
		return EINVAL;
	/* The first loader has no trampoline for the wrapper's remap window. */
	if (pa >= UINT64_C(0x1fc00000) && pa < UINT64_C(0x1fc04000))
		return EOPNOTSUPP;
	return 0;
}

/* Reject wrapping spans and input/output overlap before reading either. */
static int
mips_buffers_check(const void *out, size_t out_size, const uint64_t *in,
    size_t in_size)
{
	uintptr_t a = (uintptr_t)out, b = (uintptr_t)in;

	if (out == NULL || in == NULL || out_size == 0 || in_size == 0 ||
	    out_size - 1 > (uintptr_t)-1 - a ||
	    in_size - 1 > (uintptr_t)-1 - b || b % _Alignof(uint64_t) != 0)
		return EINVAL;
	if (a <= b ? b - a < out_size : a - b < in_size)
		return EINVAL;
	return 0;
}

int
sun60i_a733_gpu_mips_map(void *table, size_t table_size, uint64_t va,
    const uint64_t *pages, size_t page_count)
{
	uint8_t *bytes = table;
	uint32_t pte;
	uint64_t pa;
	size_t first, i, j;
	int error;

	if (table_size != A733_GPU_MIPS_PT_SIZE || page_count == 0 ||
	    page_count > MIPS_PTE_COUNT || va < A733_GPU_MIPS_HEAP_BASE ||
	    va >= A733_GPU_MIPS_HEAP_END ||
	    (va & (A733_GPU_MIPS_PAGE_SIZE - 1)) != 0 ||
	    page_count > (A733_GPU_MIPS_HEAP_END - va) /
	    A733_GPU_MIPS_PAGE_SIZE)
		return EINVAL;
	error = mips_buffers_check(table, table_size, pages,
	    page_count * sizeof(*pages));
	if (error != 0)
		return error;
	first = (size_t)((va - A733_GPU_MIPS_HEAP_BASE) /
	    A733_GPU_MIPS_PAGE_SIZE);

	/* Complete the preflight before changing even the first entry. */
	for (i = 0; i < page_count; i++) {
		error = mips_pa_check(pages[i]);
		if (error != 0)
			return error;
		if (mips_get32(bytes + (first + i) * 4) != 0)
			return EEXIST;
		for (j = 0; j < i; j++) {
			if (pages[j] == pages[i])
				return EEXIST;
		}
	}
	for (i = 0; i < MIPS_PTE_COUNT; i++) {
		pte = mips_get32(bytes + i * 4);
		if (pte == 0)
			continue;
		if ((pte & ~MIPS_PFN_MASK) != MIPS_PTE_FLAGS)
			return EINVAL;
		pa = (uint64_t)(pte & MIPS_PFN_MASK) << 6;
		error = mips_pa_check(pa);
		if (error != 0)
			return error;
		for (j = 0; j < page_count; j++) {
			if (pa == pages[j])
				return EEXIST;
		}
	}

	for (i = 0; i < page_count; i++) {
		pte = (uint32_t)(pages[i] >> 6) | MIPS_PTE_FLAGS;
		mips_put32(bytes + (first + i) * 4, pte);
	}
	return 0;
}

/* The caller must invalidate live translations before releasing the pages. */
int
sun60i_a733_gpu_mips_unmap(void *table, size_t table_size, uint64_t va,
    const uint64_t *pages, size_t page_count)
{
	uint8_t *bytes = table;
	size_t first, i;
	int error;

	if (table_size != A733_GPU_MIPS_PT_SIZE || page_count == 0 ||
	    page_count > MIPS_PTE_COUNT || va < A733_GPU_MIPS_HEAP_BASE ||
	    va >= A733_GPU_MIPS_HEAP_END ||
	    (va & (A733_GPU_MIPS_PAGE_SIZE - 1)) != 0 ||
	    page_count > (A733_GPU_MIPS_HEAP_END - va) /
	    A733_GPU_MIPS_PAGE_SIZE)
		return EINVAL;
	error = mips_buffers_check(table, table_size, pages,
	    page_count * sizeof(*pages));
	if (error != 0)
		return error;
	first = (size_t)((va - A733_GPU_MIPS_HEAP_BASE) /
	    A733_GPU_MIPS_PAGE_SIZE);
	/* Validate every owner before clearing any entry. */
	for (i = 0; i < page_count; i++) {
		error = mips_pa_check(pages[i]);
		if (error != 0)
			return error;
		if (mips_get32(bytes + (first + i) * 4) !=
		    ((uint32_t)(pages[i] >> 6) | MIPS_PTE_FLAGS))
			return ENOENT;
	}
	for (i = 0; i < page_count; i++)
		mips_put32(bytes + (first + i) * 4, 0);
	return 0;
}

int
sun60i_a733_gpu_mips_boot_data(void *out, size_t out_size, uint64_t stack_pa,
    uint64_t register_pa, const uint64_t pt_pa[A733_GPU_MIPS_PT_PAGES])
{
	uint8_t *bytes = out;
	size_t i, j;
	int error;

	if (out_size != A733_GPU_MIPS_BOOT_DATA_SIZE ||
	    register_pa != A733_GPU_MIPS_REGISTER_PA)
		return EINVAL;
	error = mips_buffers_check(out, out_size, pt_pa,
	    A733_GPU_MIPS_PT_PAGES * sizeof(*pt_pa));
	if (error != 0)
		return error;
	error = mips_pa_check(stack_pa);
	if (error != 0)
		return error;
	for (i = 0; i < A733_GPU_MIPS_PT_PAGES; i++) {
		error = mips_pa_check(pt_pa[i]);
		if (error != 0)
			return error;
		if (pt_pa[i] == stack_pa)
			return EEXIST;
		for (j = 0; j < i; j++) {
			if (pt_pa[j] == pt_pa[i])
				return EEXIST;
		}
	}

	mips_put64(bytes, stack_pa);
	mips_put64(bytes + 8, register_pa);
	for (i = 0; i < A733_GPU_MIPS_PT_PAGES; i++)
		mips_put64(bytes + 16 + i * 8, pt_pa[i]);
	mips_put32(bytes + 48, 12);
	mips_put32(bytes + 52, A733_GPU_MIPS_PT_PAGES);
	mips_put32(bytes + 56, 0);
	mips_put32(bytes + 60, 0);
	return 0;
}
