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

#ifndef _SUN60I_A733_GPU_MMU_H
#define _SUN60I_A733_GPU_MMU_H

#include <sys/types.h>
#ifndef _KERNEL
#include <stddef.h>
#include <stdint.h>
#endif

#define A733_GPU_MMU_PAGE_SIZE	4096
#define A733_GPU_MMU_PC_ENTRIES	1024
#define A733_GPU_MMU_PD_ENTRIES	512
#define A733_GPU_MMU_PT_ENTRIES	512
#define A733_GPU_MMU_PA_BITS	36
#define A733_GPU_MMU_VA_BITS	40

#define A733_GPU_MMU_PTE_READ_ONLY	UINT64_C(0x0000000000000002)
#define A733_GPU_MMU_PTE_COHERENT		UINT64_C(0x0000000000000004)
#define A733_GPU_MMU_PTE_SLC_BYPASS	UINT64_C(0x0000000000000008)
#define A733_GPU_MMU_PTE_PM_FW_PROTECT	UINT64_C(0x4000000000000000)

struct sun60i_a733_gpu_mmu_indices {
	uint16_t pc, pd, pt, offset;
};

/*
 * CPU-only Rogue GPU MMU encoders for the 36-bit PA, 40-bit VA, 4 KiB
 * profile. These are not the MIPS firmware processor's page tables.
 * No allocation, table write, DMA, cache operation, MMIO, or publication.
 * The caller owns each aligned output and serializes access to it.
 * Return positive EINVAL for an invalid argument; preserve output on error.
 * Encoded entries are native integers. Serialize them little-endian before
 * a later table publication. Zero is the invalid entry at every level.
 * These helpers validate representation, not RAM ownership or accessibility.
 */

/* Accept a byte VA and return all three table indices and its page offset. */
int sun60i_a733_gpu_mmu_indices(uint64_t,
    struct sun60i_a733_gpu_mmu_indices *);

/* Encode a valid entry for a 4 KiB-aligned PD or PT physical page. */
int sun60i_a733_gpu_mmu_pce(uint64_t, uint32_t *);
int sun60i_a733_gpu_mmu_pde(uint64_t, uint64_t *);

/*
 * Encode a valid 4 KiB leaf. flags contains only the four PTE options above.
 * Pending, PM source, virtual-physical page fields, and unknown bits fail.
 * COHERENT encodes the hardware bit; it does not establish CPU/DMA cache
 * coherence. The caller must justify that policy for the actual mapping.
 */
int sun60i_a733_gpu_mmu_pte(uint64_t, uint64_t, uint64_t *);

#endif /* _SUN60I_A733_GPU_MMU_H */
