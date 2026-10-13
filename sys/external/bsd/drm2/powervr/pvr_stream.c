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

/* The fixed BXM profile omits indirect draw, object LLS and tessellation. */
int
pvr_stream_geometry(uint64_t bvnc, const void *data, size_t size,
    uint32_t flags, struct rogue_fwif_cmd_geom *out)
{
	struct rogue_fwif_cmd_geom cmd;
	const uint8_t *p = data;
	int error;

	error = pvr_stream_header(bvnc, data, size, PVR_STREAM_GEOMETRY_SIZE, out);
	if (error != 0)
		return error;
	if ((flags & ~7U) != 0)
		return EOPNOTSUPP;
	memset(&cmd, 0, sizeof(cmd));
	cmd.regs.vdm_ctrl_stream_base = pvr_stream_u64(p + 8);
	cmd.regs.tpu_border_colour_table = pvr_stream_u64(p + 16);
	cmd.regs.ppp_ctrl = pvr_stream_u32(p + 24);
	cmd.regs.te_psg = pvr_stream_u32(p + 28);
	cmd.regs.vdm_context_resume_task0_size = pvr_stream_u32(p + 32);
	cmd.regs.view_idx = pvr_stream_u32(p + 36);
	if ((flags & 1U) != 0)
		cmd.flags |= ROGUE_GEOM_FLAGS_FIRSTKICK;
	if ((flags & 2U) != 0)
		cmd.flags |= ROGUE_GEOM_FLAGS_LASTKICK;
	if ((flags & 4U) != 0)
		cmd.flags |= ROGUE_GEOM_FLAGS_SINGLE_CORE;
	memcpy(out, &cmd, sizeof(cmd));
	return 0;
}

int
pvr_stream_fragment(uint64_t bvnc, const void *data, size_t size,
    uint32_t flags, struct rogue_fwif_cmd_frag *out, uint32_t *command_type)
{
	struct rogue_fwif_cmd_frag cmd;
	const uint8_t *p = data;
	size_t i, j;
	uint32_t type;
	int error;

	if (command_type == NULL)
		return EINVAL;
	error = pvr_stream_header(bvnc, data, size, PVR_STREAM_FRAGMENT_SIZE, out);
	if (error != 0)
		return error;
	/* Both multicore-only values are zero in the Mesa MC1 profile. */
	if ((flags & ~255U) != 0 || pvr_stream_u32(p + 368) != 0 ||
	    pvr_stream_u32(p + 380) != 0)
		return EOPNOTSUPP;
	memset(&cmd, 0, sizeof(cmd));
	cmd.regs.isp_scissor_base = pvr_stream_u64(p + 8);
	cmd.regs.isp_dbias_base = pvr_stream_u64(p + 16);
	cmd.regs.isp_oclqry_base = pvr_stream_u64(p + 24);
	cmd.regs.isp_zlsctl = pvr_stream_u64(p + 32);
	cmd.regs.isp_zload_store_base = pvr_stream_u64(p + 40);
	cmd.regs.isp_stencil_load_store_base = pvr_stream_u64(p + 48);
	for (i = 0; i < 8; i++) {
		for (j = 0; j < 3; j++)
			cmd.regs.pbe_word[i][j] =
			    pvr_stream_u64(p + 56 + (i * 3 + j) * 8);
	}
	cmd.regs.tpu_border_colour_table = pvr_stream_u64(p + 248);
	for (i = 0; i < 3; i++) {
		cmd.regs.pds_bgnd[i] = pvr_stream_u64(p + 256 + i * 8);
		cmd.regs.pds_pr_bgnd[i] = pvr_stream_u64(p + 280 + i * 8);
	}
	for (i = 0; i < 8; i++)
		cmd.regs.usc_clear_register[i] = pvr_stream_u32(p + 304 + i * 4);
	cmd.regs.usc_pixel_output_ctrl = pvr_stream_u32(p + 336);
	cmd.regs.isp_bgobjdepth = pvr_stream_u32(p + 340);
	cmd.regs.isp_bgobjvals = pvr_stream_u32(p + 344);
	cmd.regs.isp_aa = pvr_stream_u32(p + 348);
	cmd.regs.isp_ctl = pvr_stream_u32(p + 352);
	cmd.regs.event_pixel_pds_info = pvr_stream_u32(p + 356);
	cmd.regs.view_idx = pvr_stream_u32(p + 360);
	cmd.regs.event_pixel_pds_data = pvr_stream_u32(p + 364);
	cmd.zls_stride = pvr_stream_u32(p + 372);
	cmd.sls_stride = pvr_stream_u32(p + 376);
	if ((flags & 1U) != 0)
		cmd.flags |= ROGUE_FRAG_FLAGS_SINGLE_CORE;
	if ((flags & 2U) != 0)
		cmd.flags |= ROGUE_FRAG_FLAGS_DEPTHBUFFER;
	if ((flags & 4U) != 0)
		cmd.flags |= ROGUE_FRAG_FLAGS_STENCILBUFFER;
	if ((flags & 8U) != 0)
		cmd.flags |= ROGUE_FRAG_FLAGS_PREVENT_CDM_OVERLAP;
	if ((flags & 16U) != 0)
		cmd.flags |= ROGUE_FRAG_FLAGS_SCRATCHBUFFER;
	if ((flags & 32U) != 0)
		cmd.flags |= ROGUE_FRAG_FLAGS_GET_VIS_RESULTS;
	if ((flags & 128U) != 0)
		cmd.flags |= ROGUE_FRAG_FLAGS_DISABLE_PIXELMERGE;
	/* PARTIAL_RENDER selects a CCCB command, not a firmware flag bit. */
	type = (flags & 64U) != 0 ? ROGUE_FWIF_CCB_CMD_TYPE_FRAG_PR :
	    ROGUE_FWIF_CCB_CMD_TYPE_FRAG;
	memcpy(out, &cmd, sizeof(cmd));
	*command_type = type;
	return 0;
}

int
pvr_stream_transfer(uint64_t bvnc, const void *data, size_t size,
    uint32_t flags, struct rogue_fwif_cmd_transfer *out)
{
	struct rogue_fwif_cmd_transfer cmd;
	const uint8_t *p = data;
	size_t i;
	int error;

	error = pvr_stream_header(bvnc, data, size, PVR_STREAM_TRANSFER_SIZE, out);
	if (error != 0)
		return error;
	if ((flags & ~1U) != 0)
		return EOPNOTSUPP;
	memset(&cmd, 0, sizeof(cmd));
	cmd.regs.pds_bgnd0_base = pvr_stream_u64(p + 8);
	cmd.regs.pds_bgnd1_base = pvr_stream_u64(p + 16);
	cmd.regs.pds_bgnd3_sizeinfo = pvr_stream_u64(p + 24);
	cmd.regs.isp_mtile_base = pvr_stream_u64(p + 32);
	for (i = 0; i < 9; i++)
		cmd.regs.pbe_wordx_mrty[i] = pvr_stream_u64(p + 40 + i * 8);
	cmd.regs.isp_bgobjvals = pvr_stream_u32(p + 112);
	cmd.regs.usc_pixel_output_ctrl = pvr_stream_u32(p + 116);
	cmd.regs.usc_clear_register0 = pvr_stream_u32(p + 120);
	cmd.regs.usc_clear_register1 = pvr_stream_u32(p + 124);
	cmd.regs.usc_clear_register2 = pvr_stream_u32(p + 128);
	cmd.regs.usc_clear_register3 = pvr_stream_u32(p + 132);
	cmd.regs.isp_mtile_size = pvr_stream_u32(p + 136);
	cmd.regs.isp_render_origin = pvr_stream_u32(p + 140);
	cmd.regs.isp_ctl = pvr_stream_u32(p + 144);
	cmd.regs.isp_aa = pvr_stream_u32(p + 148);
	cmd.regs.event_pixel_pds_info = pvr_stream_u32(p + 152);
	cmd.regs.event_pixel_pds_code = pvr_stream_u32(p + 156);
	cmd.regs.event_pixel_pds_data = pvr_stream_u32(p + 160);
	cmd.regs.isp_render = pvr_stream_u32(p + 164);
	cmd.regs.isp_rgn = pvr_stream_u32(p + 168);
	/* This holds surface dimensions even when the runtime has only one core. */
	cmd.regs.frag_screen = pvr_stream_u32(p + 172);
	if ((flags & 1U) != 0)
		cmd.flags |= ROGUE_TRANSFER_FLAGS_SINGLE_CORE;
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
