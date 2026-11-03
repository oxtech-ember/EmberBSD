/* SPDX-License-Identifier: MIT */
/* Origin: EmberBSD common PowerVR compute serializer, from A733. */
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

#ifndef _SUN60I_PVR_FWCOMPUTE_H
#define _SUN60I_PVR_FWCOMPUTE_H

#include "fwif/pvr_rogue_fwif.h"
#include "fwif/pvr_rogue_fwif_client.h"
#include "pvr_fwinfo.h"

#define PVR_FWCOMPUTE_CCCB_SIZE	32768U
#define PVR_FWCOMPUTE_USED_BYTES	184U

enum pvr_fwcompute_object {
	PVR_FWCOMPUTE_CONTEXT,
	PVR_FWCOMPUTE_CTL,
	PVR_FWCOMPUTE_CCCB,
	PVR_FWCOMPUTE_STATE,
	PVR_FWCOMPUTE_TIMELINE,
	PVR_FWCOMPUTE_VM_CONTEXT,
	PVR_FWCOMPUTE_OBJECTS
};

enum pvr_fwcompute_program {
	PVR_FWCOMPUTE_USC,
	PVR_FWCOMPUTE_PDS,
	PVR_FWCOMPUTE_STREAM,
	PVR_FWCOMPUTE_PROGRAMS
};

struct pvr_fwcompute_fwspan {
	uint32_t addr;
	uint32_t size;
};

struct pvr_fwcompute_program_span {
	const void *bytes;
	size_t size;
	uint64_t gpu_va;
};

struct pvr_fwcompute_inputs {
	/* Exact sizes, 8-byte FW alignment, independent mappings. */
	struct pvr_fwcompute_fwspan fw[PVR_FWCOMPUTE_OBJECTS];
	/* Caller supplies actual compiler output. */
	struct pvr_fwcompute_program_span
	    program[PVR_FWCOMPUTE_PROGRAMS];
	struct rogue_fwif_static_computecontext_state static_state;
	struct rogue_fwif_cmd_compute command;
	uint32_t priority;
	uint32_t pid;
	uint32_t context_id;
	uint32_t job_id;
};

/* Allocate this 33 KiB object from caller-owned heap, never kernel stack. */
struct pvr_fwcompute_image {
	struct rogue_fwif_fwcomputecontext context;
	struct rogue_fwif_cccb_ctl ctl;
	struct rogue_fwif_compute_ctx_state state;
	uint32_t timeline;
	uint8_t cccb[PVR_FWCOMPUTE_CCCB_SIZE] PVR_FW_ALIGNED(8);
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
int pvr_fwcompute_prepare(enum pvr_fw_profile,
    const struct pvr_fwcompute_inputs *,
    struct pvr_fwcompute_image *, size_t);

#endif /* _SUN60I_PVR_FWCOMPUTE_H */
