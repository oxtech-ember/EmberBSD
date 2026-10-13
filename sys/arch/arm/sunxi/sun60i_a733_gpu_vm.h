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
 * GPU page tables and firmware memory context: Linux PowerVR driver,
 * 7b63ef2d55f24519e7e9e5f4d15dbea03f126e40, MIT license option.
 * Native CPU-owned allocation: EmberBSD, AI-assisted. See a733-gpu-vm.md.
 */

#ifndef _SUN60I_A733_GPU_VM_H
#define _SUN60I_A733_GPU_VM_H

#include <sys/types.h>
#include <sys/bus.h>
#include <arm/sunxi/sun60i_a733_gpu_fwinfo.h>
#include <arm/sunxi/sun60i_a733_gpu_mmu.h>

#define A733_GPU_VM_GENERAL_BASE	UINT64_C(0x8000000000)
#define A733_GPU_VM_GENERAL_END	UINT64_C(0xa000000000)

enum sun60i_a733_gpu_vm_page {
	A733_GPU_VM_PC, A733_GPU_VM_PD, A733_GPU_VM_PT,
	A733_GPU_VM_SCRATCH, A733_GPU_VM_FW_CONTEXT, A733_GPU_VM_PAGE_COUNT
};

struct sun60i_a733_gpu_vm_dma {
	bus_dma_segment_t segment;
	bus_dmamap_t map;
	void *cpu;
	bus_addr_t address;
	int nsegments;
	bool loaded, coherent;
};

struct sun60i_a733_gpu_vm {
	bus_dma_tag_t dmat;
	struct sun60i_a733_gpu_vm_dma page[A733_GPU_VM_PAGE_COUNT];
	uint64_t scratch_va;
	struct sun60i_a733_gpu_mmu_indices indices;
	bool prepared, retained;
};

/*
 * Prepare five separate 4 KiB DMA pages: PC, PD, PT, scratch, FW context.
 * Require parsed BVNC 36.56.104.183 metadata with PA36, VA40, and 4 KiB pages.
 * scratch_va must select one aligned page in the pinned general heap.
 * Use one RW leaf with GPU PTE options zero. CPU mappings are requested and
 * verified coherent without bouncing. These are separate cache attributes.
 * All output entries and the 32-byte FW context use little-endian encoding.
 * No FW virtual address is assigned. No MIPS table is read or changed.
 * No DMA sync, MMIO, or address publication occurs.
 *
 * Call from sleepable context. Serialize all access; inputs must stay valid
 * and unchanged throughout the call. out must point to a NULL owner slot,
 * separate from info. Positive errno; keep out and info unchanged on error.
 * EBUSY: occupied output slot. EOPNOTSUPP: unsupported profile or coherence.
 * EINVAL: malformed arguments or DMA backend result. Propagate DMA errors.
 */
int sun60i_a733_gpu_vm_prepare(bus_dma_tag_t,
    const struct sun60i_a733_gpu_fwinfo *, uint64_t,
    struct sun60i_a733_gpu_vm **);

/*
 * Set a sticky lifetime guard before any later publication attempt.
 * This does not sync memory, publish addresses, or prove device acceptance.
 * A prepared owner is required. Repeat calls return EBUSY unchanged.
 * There is no release/reset API for this guard in the initial scope.
 */
int sun60i_a733_gpu_vm_retain(struct sun60i_a733_gpu_vm *);

/*
 * Release a CPU-owned prepared object, then clear the caller's owner slot.
 * NULL and a NULL slot are accepted. The slot must not overlap the owner
 * or its pages. EBUSY after retain preserves all five pages and owner fields.
 * The caller must not bypass retain or reclaim published memory directly.
 */
int sun60i_a733_gpu_vm_destroy(struct sun60i_a733_gpu_vm **);

#endif /* _SUN60I_A733_GPU_VM_H */
