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
 * Context and queue initialization: Linux PowerVR driver,
 * 7b63ef2d55f24519e7e9e5f4d15dbea03f126e40, MIT license option.
 * Native CPU serializer: EmberBSD, AI-assisted. See a733-gpu-fwcompute.md.
 */

#ifndef _SUN60I_A733_GPU_FWCOMPUTE_H
#define _SUN60I_A733_GPU_FWCOMPUTE_H

#include "powervr/pvr_rogue_fwif.h"
#include "powervr/pvr_rogue_fwif_client.h"

#define A733_GPU_FWCOMPUTE_CCCB_SIZE	32768U
#define A733_GPU_FWCOMPUTE_USED_BYTES	184U

enum sun60i_a733_gpu_fwcompute_object {
	A733_GPU_FWCOMPUTE_CONTEXT,
	A733_GPU_FWCOMPUTE_CTL,
	A733_GPU_FWCOMPUTE_CCCB,
	A733_GPU_FWCOMPUTE_STATE,
	A733_GPU_FWCOMPUTE_TIMELINE,
	A733_GPU_FWCOMPUTE_VM_CONTEXT,
	A733_GPU_FWCOMPUTE_OBJECTS
};

enum sun60i_a733_gpu_fwcompute_program {
	A733_GPU_FWCOMPUTE_USC,
	A733_GPU_FWCOMPUTE_PDS,
	A733_GPU_FWCOMPUTE_STREAM,
	A733_GPU_FWCOMPUTE_PROGRAMS
};

struct sun60i_a733_gpu_fwcompute_fwspan {
	uint32_t addr;
	uint32_t size;
};

struct sun60i_a733_gpu_fwcompute_program_span {
	const void *bytes;
	size_t size;
	uint64_t gpu_va;
};

struct sun60i_a733_gpu_fwcompute_inputs {
	/* Exact sizes, 8-byte FW alignment, independent mappings. */
	struct sun60i_a733_gpu_fwcompute_fwspan fw[A733_GPU_FWCOMPUTE_OBJECTS];
	/* Caller supplies actual compiler output. */
	struct sun60i_a733_gpu_fwcompute_program_span
	    program[A733_GPU_FWCOMPUTE_PROGRAMS];
	struct rogue_fwif_static_computecontext_state static_state;
	struct rogue_fwif_cmd_compute command;
	uint32_t priority;
	uint32_t pid;
	uint32_t context_id;
	uint32_t job_id;
};

/* Allocate this 33 KiB object from caller-owned heap, never kernel stack. */
struct sun60i_a733_gpu_fwcompute_image {
	struct rogue_fwif_fwcomputecontext context;
	struct rogue_fwif_cccb_ctl ctl;
	struct rogue_fwif_compute_ctx_state state;
	uint32_t timeline;
	uint8_t cccb[A733_GPU_FWCOMPUTE_CCCB_SIZE] PVR_FW_ALIGNED(8);
	struct rogue_fwif_kccb_cmd kick;
	uint32_t used_bytes;
};

/*
 * Prepare one fresh CPU image on a 64-bit little-endian host. The caller
 * owns readable inputs and writable output and serializes their access.
 * No allocation, DMA, GPU/MIPS mapping, barrier, MMIO, or queue publication.
 * Output is unchanged on every error. image_size must equal sizeof(*image).
 * All CPU spans must be distinct; FW spans must be distinct and prevalidated
 * against the actual firmware heap by the caller. This validates numerical
 * representation, not RAM ownership, page tables, or GPU instructions.
 * Nonempty program spans and a nonzero static state are required. These are
 * not a compiler attestation: hardware use requires a real Mesa manifest.
 * The VM context is external; this helper only serializes its FW address.
 * EINVAL: invalid representation, flags, lengths, alignment, or aliases.
 * EOPNOTSUPP: optional command fields outside the initial direct-CDM scope.
 * Padding and reserved bytes in typed inputs are not read or copied.
 */
int sun60i_a733_gpu_fwcompute_prepare(
    const struct sun60i_a733_gpu_fwcompute_inputs *,
    struct sun60i_a733_gpu_fwcompute_image *, size_t);

#endif /* _SUN60I_A733_GPU_FWCOMPUTE_H */
