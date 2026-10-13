/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: Linux 7b63ef2d55f24519e7e9e5f4d15dbea03f126e40,
 * drivers/gpu/drm/imagination/pvr_stream.c, pvr_stream_defs.c, and pvr_job.c.
 * This adaptation selects the MIT alternative. It is AI-assisted.
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
 */

#include <sys/types.h>
#ifdef _KERNEL
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>
#else
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#endif

#include <arm/sunxi/powervr/pvr_rogue_fwif.h>
#include <arm/sunxi/powervr/pvr_rogue_fwif_client.h>
#include "pvr_stream.h"

/*
 * Mesa 26.2.4, common/device_info/bxm-4-64.h: the accepted profile has
 * TPU_DM_GLOBAL_REGISTERS and GPU_MULTICORE_SUPPORT. It has no user-mode CDM
 * queue, Morton mode, cluster grouping, or BRN49927. The native runtime
 * currently exposes one core. A different stream profile needs its own gate.
 */
static uint32_t
pvr_stream_u32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
	    (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t
pvr_stream_u64(const uint8_t *p)
{
	return pvr_stream_u32(p) | (uint64_t)pvr_stream_u32(p + 4) << 32;
}

static int
pvr_stream_header(uint64_t bvnc, const void *data, size_t size,
    size_t expected, const void *out)
{
	const uint8_t *p = data;

	if (data == NULL || out == NULL)
		return EINVAL;
	if (bvnc != PVR_STREAM_BVNC)
		return EOPNOTSUPP;
	if (size < expected || pvr_stream_u32(p) != expected ||
	    pvr_stream_u32(p + 4) != 0)
		return EINVAL;
	if (size != expected)
		return EOPNOTSUPP;
	return 0;
}

int
pvr_stream_compute(uint64_t bvnc, const void *data, size_t size,
    uint32_t flags, struct rogue_fwif_cmd_compute *out)
{
	struct rogue_fwif_cmd_compute cmd;
	const uint8_t *p = data;
	int error;

	error = pvr_stream_header(bvnc, data, size, PVR_STREAM_COMPUTE_SIZE, out);
	if (error != 0)
		return error;
	/* UAPI PREVENT_ALL_OVERLAP and SINGLE_CORE, not firmware flag numbers. */
	if ((flags & ~3U) != 0 || pvr_stream_u32(p + 40) != 0)
		return EOPNOTSUPP;
	memset(&cmd, 0, sizeof(cmd));
	cmd.regs.tpu_border_colour_table = pvr_stream_u64(p + 8);
	cmd.regs.cdm_ctrl_stream_base = pvr_stream_u64(p + 16);
	cmd.regs.cdm_context_state_base_addr = pvr_stream_u64(p + 24);
	cmd.regs.cdm_resume_pds1 = pvr_stream_u32(p + 32);
	cmd.regs.tpu_tag_cdm_ctrl = pvr_stream_u32(p + 36);
	if ((flags & 1U) != 0)
		cmd.flags |= ROGUE_COMPUTE_FLAG_PREVENT_ALL_OVERLAP;
	if ((flags & 2U) != 0)
		cmd.flags |= ROGUE_COMPUTE_FLAG_SINGLE_CORE;
	memcpy(out, &cmd, sizeof(cmd));
	return 0;
}

int
pvr_stream_static_compute(uint64_t bvnc, const void *data, size_t size,
    struct rogue_fwif_cdm_registers_cswitch *out)
{
	struct rogue_fwif_cdm_registers_cswitch state;
	const uint8_t *p = data;
	int error;

	error = pvr_stream_header(bvnc, data, size, PVR_STREAM_STATIC_COMPUTE_SIZE,
	    out);
	if (error != 0)
		return error;
	memset(&state, 0, sizeof(state));
	state.cdmreg_cdm_context_pds0 = pvr_stream_u64(p + 8);
	state.cdmreg_cdm_context_pds1 = pvr_stream_u64(p + 16);
	state.cdmreg_cdm_terminate_pds = pvr_stream_u64(p + 24);
	state.cdmreg_cdm_terminate_pds1 = pvr_stream_u64(p + 32);
	state.cdmreg_cdm_resume_pds0 = pvr_stream_u64(p + 40);
	state.cdmreg_cdm_context_pds0_b = pvr_stream_u64(p + 48);
	state.cdmreg_cdm_resume_pds0_b = pvr_stream_u64(p + 56);
	memcpy(out, &state, sizeof(state));
	return 0;
}

int
pvr_stream_static_render(uint64_t bvnc, const void *data, size_t size,
    struct rogue_fwif_static_rendercontext_state *out)
{
	struct rogue_fwif_static_rendercontext_state state;
	struct rogue_fwif_geom_registers_caswitch *regs;
	const uint8_t *p = data;
	size_t i, offset;
	int error;

	error = pvr_stream_header(bvnc, data, size, PVR_STREAM_STATIC_RENDER_SIZE,
	    out);
	if (error != 0)
		return error;
	memset(&state, 0, sizeof(state));
	regs = &state.ctxswitch_regs[0];
	regs->geom_reg_vdm_context_state_base_addr = pvr_stream_u64(p + 8);
	regs->geom_reg_vdm_context_state_resume_addr = pvr_stream_u64(p + 16);
	regs->geom_reg_ta_context_state_base_addr = pvr_stream_u64(p + 24);
	for (i = 0; i < 2; i++) {
		offset = 32 + i * 80;
		/* The wire and firmware task orders differ. Do not memcpy them. */
		regs->geom_state[i].geom_reg_vdm_context_store_task0 =
		    pvr_stream_u64(p + offset);
		regs->geom_state[i].geom_reg_vdm_context_store_task1 =
		    pvr_stream_u64(p + offset + 8);
		regs->geom_state[i].geom_reg_vdm_context_store_task2 =
		    pvr_stream_u64(p + offset + 16);
		regs->geom_state[i].geom_reg_vdm_context_store_task3 =
		    pvr_stream_u64(p + offset + 24);
		regs->geom_state[i].geom_reg_vdm_context_store_task4 =
		    pvr_stream_u64(p + offset + 32);
		regs->geom_state[i].geom_reg_vdm_context_resume_task0 =
		    pvr_stream_u64(p + offset + 40);
		regs->geom_state[i].geom_reg_vdm_context_resume_task1 =
		    pvr_stream_u64(p + offset + 48);
		regs->geom_state[i].geom_reg_vdm_context_resume_task2 =
		    pvr_stream_u64(p + offset + 56);
		regs->geom_state[i].geom_reg_vdm_context_resume_task3 =
		    pvr_stream_u64(p + offset + 64);
		regs->geom_state[i].geom_reg_vdm_context_resume_task4 =
		    pvr_stream_u64(p + offset + 72);
	}
	memcpy(out, &state, sizeof(state));
	return 0;
}

int
pvr_stream_static_transfer(uint64_t bvnc, const void *data, size_t size)
{
	/* Linux init_transfer_fw_objs ignores the pointer for an empty stream. */
	(void)data;
	if (bvnc != PVR_STREAM_BVNC)
		return EOPNOTSUPP;
	return size == 0 ? 0 : EINVAL;
}
