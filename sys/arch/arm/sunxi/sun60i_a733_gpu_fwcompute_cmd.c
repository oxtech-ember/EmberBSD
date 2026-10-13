/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted native decoder. */
/*
 * Wire facts: Mesa 26.2.4 pvr_arch_job_compute.c, rogue/{cr,kmd_stream}.xml;
 * Linux PowerVR 7b63ef2d55f24519e7e9e5f4d15dbea03f126e40,
 * pvr_stream_defs.c and pvr_job.c (MIT license option).
 * The producer supplies register values. This decoder does not invent them.
 */
#include <sys/types.h>
#ifdef _KERNEL
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>
#else
#include <errno.h>
#include <string.h>
#endif

#include "sun60i_a733_gpu_fwcompute_cmd.h"

static uint32_t
fwcmd_get32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
	    (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t
fwcmd_get64(const uint8_t *p)
{
	return fwcmd_get32(p) | (uint64_t)fwcmd_get32(p + 4) << 32;
}

int
sun60i_a733_gpu_fwcompute_cmd_decode(const void *data, size_t size,
    uint64_t bvnc, uint64_t stream_va, uint64_t resume_va, uint64_t border_va,
    struct rogue_fwif_cmd_compute *out)
{
	struct rogue_fwif_cmd_compute cmd;
	const uint8_t *p = data;
	uint32_t pds1, flags;

	if (data == NULL || out == NULL || size != A733_GPU_FWCOMPUTE_COMMAND_BYTES)
		return EINVAL;
	if (bvnc != A733_GPU_FWCOMPUTE_BVNC)
		return EOPNOTSUPP;
	if (stream_va == 0 || (stream_va & 3) != 0 || stream_va >> 40 != 0 ||
	    resume_va == 0 || (resume_va & 15) != 0 || resume_va >> 40 != 0 ||
	    border_va == 0 || (border_va & 63) != 0 || border_va >> 40 != 0 ||
	    fwcmd_get32(p) != A733_GPU_FWCOMPUTE_STREAM_BYTES ||
	    fwcmd_get32(p + 4) != 0 || fwcmd_get64(p + 8) != border_va >> 2 ||
	    fwcmd_get64(p + 16) != stream_va ||
	    fwcmd_get64(p + 24) != resume_va)
		return EINVAL;
	/* This BVNC has no Morton, cluster, user-queue, or BRN49927 fields. */
	if (fwcmd_get32(p + 36) != 0 || fwcmd_get32(p + 40) != 0)
		return EOPNOTSUPP;
	pds1 = fwcmd_get32(p + 32);
	flags = fwcmd_get32(p + 44);
	/* Mesa emits shared common storage, unified allocation, and load data. */
	if ((pds1 & ~UINT32_C(0x07fff87e)) != 0 ||
	    (pds1 & UINT32_C(0x00100000)) == 0 ||
	    (pds1 & UINT32_C(0x07e00000)) == 0 || (pds1 & 0x7eU) == 0 ||
	    (flags & ~3U) != 0)
		return EINVAL;
	memset(&cmd, 0, sizeof(cmd));
	cmd.regs.tpu_border_colour_table = fwcmd_get64(p + 8);
	cmd.regs.cdm_ctrl_stream_base = stream_va;
	cmd.regs.cdm_context_state_base_addr = resume_va;
	cmd.regs.cdm_resume_pds1 = pds1;
	if ((flags & 1U) != 0)
		cmd.flags |= ROGUE_COMPUTE_FLAG_PREVENT_ALL_OVERLAP;
	if ((flags & 2U) != 0)
		cmd.flags |= ROGUE_COMPUTE_FLAG_SINGLE_CORE;
	memcpy(out, &cmd, sizeof(cmd));
	return 0;
}
