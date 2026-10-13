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

#ifndef _SUN60I_A733_GPU_MIPS_H
#define _SUN60I_A733_GPU_MIPS_H

#include <sys/types.h>
#ifndef _KERNEL
#include <stddef.h>
#include <stdint.h>
#endif

#define A733_GPU_MIPS_PAGE_SIZE		4096
#define A733_GPU_MIPS_PT_SIZE		16384
#define A733_GPU_MIPS_PT_PAGES		4
#define A733_GPU_MIPS_BOOT_DATA_SIZE	64
#define A733_GPU_MIPS_HEAP_BASE		UINT64_C(0xc0000000)
#define A733_GPU_MIPS_HEAP_END		UINT64_C(0xc1000000)
#define A733_GPU_MIPS_REGISTER_PA	UINT64_C(0x01800000)

/*
 * Pure initial-boot encoders. No allocation, I/O, DMA, or cache operations.
 * The caller owns real buffers and serializes all access to them. Inputs
 * must remain stable for the entire call. Byte outputs may be unaligned;
 * native uint64_t input arrays must be aligned. Sizes must match exactly.
 * Both functions return positive errno and preserve outputs on every error.
 * EINVAL: malformed argument, range, prior PTE, or overlapping buffers.
 * EEXIST: occupied target PTE or physical page alias.
 * EOPNOTSUPP: PA in the wrapper remap window 0x1fc00000..0x1fc03fff.
 * PA aliases are an initial-boot restriction, not a limit of the MIPS ABI.
 */

/*
 * Write 1..4096 little-endian PTEs for 4K pages in the 16 MiB firmware heap.
 * PAs must fit 36 bits. Prior entries must be zero or use the same encoding.
 * New pages must not alias each other or any prior mapped physical page.
 * The caller must initially zero the table. There is no replacement/unmap.
 */
int sun60i_a733_gpu_mips_map(void *, size_t, uint64_t, const uint64_t *,
    size_t);

/*
 * Write the 64-byte little-endian rogue_mipsfw_boot_data wire structure.
 * Supply one stack page and four distinct 4K page table pages, all 36-bit.
 * Stack and table pages must not alias. register_pa must be 0x01800000.
 * The caller verifies RAM ownership and excludes device MMIO allocations.
 */
int sun60i_a733_gpu_mips_boot_data(void *, size_t, uint64_t, uint64_t,
    const uint64_t [A733_GPU_MIPS_PT_PAGES]);

#endif /* _SUN60I_A733_GPU_MIPS_H */
