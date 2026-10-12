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

#include <sys/types.h>
#ifdef _KERNEL
#include <sys/errno.h>
#include <sys/systm.h>
#else
#include <errno.h>
#include <string.h>
#endif

#include "sun60i_a733_gpu_fwcompute.h"
#include "powervr/pvr_rogue_fwif_check.h"
#include "powervr/pvr_rogue_fwif_shared_check.h"
#include "powervr/pvr_rogue_fwif_client_check.h"

/* Additional checks derived from the original struct, not upstream checks. */
SIZE_CHECK(struct rogue_fwif_fwcomputecontext, 232);
OFFSET_CHECK(struct rogue_fwif_fwcomputecontext, cdm_context, 0);
OFFSET_CHECK(struct rogue_fwif_fwcomputecontext,
    static_compute_context_state, 152);
OFFSET_CHECK(struct rogue_fwif_fwcomputecontext, work_est_ccb_submitted, 208);
OFFSET_CHECK(struct rogue_fwif_fwcomputecontext, compute_ctx_flags, 212);
OFFSET_CHECK(struct rogue_fwif_fwcomputecontext, wgp_state, 216);
OFFSET_CHECK(struct rogue_fwif_fwcomputecontext, wgp_checksum, 220);
OFFSET_CHECK(struct rogue_fwif_fwcomputecontext, core_mask_a, 224);
OFFSET_CHECK(struct rogue_fwif_fwcomputecontext, core_mask_b, 228);
_Static_assert(2 * sizeof(struct rogue_fwif_ccb_cmd_header) +
    sizeof(struct rogue_fwif_cmd_compute) + sizeof(struct rogue_fwif_ufo) ==
    A733_GPU_FWCOMPUTE_USED_BYTES, "initial client CCB size");

static bool
fwcompute_cpu_span(const void *p, size_t size, size_t alignment)
{
	uintptr_t addr = (uintptr_t)p;

	return p != NULL && size != 0 && (addr & (alignment - 1)) == 0 &&
	    size - 1 <= UINTPTR_MAX - addr;
}

/* Inclusive ends avoid overflow for a valid span ending at UINTPTR_MAX. */
static bool
fwcompute_overlap(uint64_t a, uint64_t asize, uint64_t b, uint64_t bsize)
{
	return a <= b + bsize - 1 && b <= a + asize - 1;
}

static int
fwcompute_validate(const struct sun60i_a733_gpu_fwcompute_inputs *in,
    const struct sun60i_a733_gpu_fwcompute_image *image, size_t size)
{
	static const uint32_t fw_sizes[A733_GPU_FWCOMPUTE_OBJECTS] = {
		sizeof(struct rogue_fwif_fwcomputecontext),
		sizeof(struct rogue_fwif_cccb_ctl),
		A733_GPU_FWCOMPUTE_CCCB_SIZE,
		sizeof(struct rogue_fwif_compute_ctx_state), sizeof(uint32_t),
		sizeof(struct rogue_fwif_fwmemcontext)
	};
	const struct rogue_fwif_cdm_registers_cswitch *s;
	const struct rogue_fwif_cmd_compute *c;
	unsigned int i, j;

	if (size != sizeof(*image) ||
	    !fwcompute_cpu_span(image, size,
	    _Alignof(struct sun60i_a733_gpu_fwcompute_image)) ||
	    !fwcompute_cpu_span(in, sizeof(*in),
	    _Alignof(struct sun60i_a733_gpu_fwcompute_inputs)) ||
	    fwcompute_overlap((uintptr_t)in, sizeof(*in),
	    (uintptr_t)image, size))
		return EINVAL;
	for (i = 0; i < A733_GPU_FWCOMPUTE_OBJECTS; i++) {
		if (in->fw[i].size != fw_sizes[i] || in->fw[i].addr == 0 ||
		    (in->fw[i].addr & 7) != 0 ||
		    (uint64_t)in->fw[i].addr + in->fw[i].size >
		    UINT64_C(0x100000000))
			return EINVAL;
		for (j = 0; j < i; j++) {
			if (fwcompute_overlap(in->fw[i].addr, in->fw[i].size,
			    in->fw[j].addr, in->fw[j].size))
				return EINVAL;
		}
	}
	for (i = 0; i < A733_GPU_FWCOMPUTE_PROGRAMS; i++) {
		const struct sun60i_a733_gpu_fwcompute_program_span *p =
		    &in->program[i];

		if (!fwcompute_cpu_span(p->bytes, p->size, 4) ||
		    (p->size & 3) != 0 || p->size > UINT32_MAX ||
		    p->gpu_va == 0 || p->gpu_va >= (UINT64_C(1) << 40) ||
		    p->size > (UINT64_C(1) << 40) - p->gpu_va ||
		    (p->gpu_va &
		    (i == A733_GPU_FWCOMPUTE_STREAM ? 3 : 15)) != 0 ||
		    fwcompute_overlap((uintptr_t)p->bytes, p->size,
		    (uintptr_t)in, sizeof(*in)) ||
		    fwcompute_overlap((uintptr_t)p->bytes, p->size,
		    (uintptr_t)image, size))
			return EINVAL;
		for (j = 0; j < i; j++) {
			const struct sun60i_a733_gpu_fwcompute_program_span *q =
			    &in->program[j];

			if (fwcompute_overlap((uintptr_t)p->bytes, p->size,
			    (uintptr_t)q->bytes, q->size) ||
			    fwcompute_overlap(p->gpu_va, p->size,
			    q->gpu_va, q->size))
				return EINVAL;
		}
	}
	s = &in->static_state.ctxswitch_regs;
	c = &in->command;
	if (in->priority > 2 || c->common.frame_num != 0 ||
	    (c->flags & ~(ROGUE_COMPUTE_FLAG_PREVENT_ALL_OVERLAP |
	    ROGUE_COMPUTE_FLAG_SINGLE_CORE)) != 0 ||
	    c->regs.cdm_ctrl_stream_base !=
	    in->program[A733_GPU_FWCOMPUTE_STREAM].gpu_va ||
	    c->regs.cdm_context_state_base_addr == 0 ||
	    c->regs.cdm_context_state_base_addr >= (UINT64_C(1) << 40) ||
	    (c->regs.cdm_context_state_base_addr & 15) != 0 ||
	    (s->cdmreg_cdm_context_pds0 | s->cdmreg_cdm_context_pds1 |
	    s->cdmreg_cdm_terminate_pds | s->cdmreg_cdm_terminate_pds1 |
	    s->cdmreg_cdm_resume_pds0 | s->cdmreg_cdm_context_pds0_b |
	    s->cdmreg_cdm_resume_pds0_b) == 0)
		return EINVAL;
	/* Direct CDM only. Never silently clear unsupported supplied state. */
	if (c->regs.cdm_cb_queue != 0 || c->regs.cdm_cb_base != 0 ||
	    c->regs.cdm_cb != 0 || c->regs.tpu != 0 ||
	    c->regs.cdm_item != 0 || c->regs.compute_cluster != 0 ||
	    c->num_temp_regions != 0 || c->stream_start_offset != 0 ||
	    c->execute_count != 0)
		return EOPNOTSUPP;
	return 0;
}

int
sun60i_a733_gpu_fwcompute_prepare(
    const struct sun60i_a733_gpu_fwcompute_inputs *in,
    struct sun60i_a733_gpu_fwcompute_image *image, size_t size)
{
	struct rogue_fwif_ccb_cmd_header header;
	struct rogue_fwif_cmd_compute command;
	struct rogue_fwif_fwcommoncontext *common;
	struct rogue_fwif_cdm_registers_cswitch *dst;
	const struct rogue_fwif_cdm_registers_cswitch *src;
	struct rogue_fwif_kccb_cmd_kick_data *kick;
	struct rogue_fwif_ufo update;
	int error;

	error = fwcompute_validate(in, image, size);
	if (error != 0)
		return error;

	/* No fallible operation follows. Do not put this image on the stack. */
	memset(image, 0, size);
	common = &image->context.cdm_context;
	common->ccbctl_fw_addr = in->fw[A733_GPU_FWCOMPUTE_CTL].addr;
	common->ccb_fw_addr = in->fw[A733_GPU_FWCOMPUTE_CCCB].addr;
	common->context_state_addr = in->fw[A733_GPU_FWCOMPUTE_STATE].addr;
	common->fw_mem_context_fw_addr =
	    in->fw[A733_GPU_FWCOMPUTE_VM_CONTEXT].addr;
	common->dm = PVR_FWIF_DM_CDM;
	common->priority = in->priority;
	common->max_deadline_ms = 30000;
	common->pid = in->pid;
	common->server_common_context_id = in->context_id;

	src = &in->static_state.ctxswitch_regs;
	dst = &image->context.static_compute_context_state.ctxswitch_regs;
	dst->cdmreg_cdm_context_pds0 = src->cdmreg_cdm_context_pds0;
	dst->cdmreg_cdm_context_pds1 = src->cdmreg_cdm_context_pds1;
	dst->cdmreg_cdm_terminate_pds = src->cdmreg_cdm_terminate_pds;
	dst->cdmreg_cdm_terminate_pds1 = src->cdmreg_cdm_terminate_pds1;
	dst->cdmreg_cdm_resume_pds0 = src->cdmreg_cdm_resume_pds0;
	dst->cdmreg_cdm_context_pds0_b = src->cdmreg_cdm_context_pds0_b;
	dst->cdmreg_cdm_resume_pds0_b = src->cdmreg_cdm_resume_pds0_b;
	image->ctl.wrap_mask = A733_GPU_FWCOMPUTE_CCCB_SIZE - 1;

	memset(&command, 0, sizeof(command));
	command.regs.tpu_border_colour_table =
	    in->command.regs.tpu_border_colour_table;
	command.regs.cdm_ctrl_stream_base =
	    in->command.regs.cdm_ctrl_stream_base;
	command.regs.cdm_context_state_base_addr =
	    in->command.regs.cdm_context_state_base_addr;
	command.regs.cdm_resume_pds1 = in->command.regs.cdm_resume_pds1;
	command.regs.tpu_tag_cdm_ctrl = in->command.regs.tpu_tag_cdm_ctrl;
	command.flags = in->command.flags;
	memset(&header, 0, sizeof(header));
	header.cmd_type = ROGUE_FWIF_CCB_CMD_TYPE_CDM;
	header.cmd_size = sizeof(command);
	header.ext_job_ref = in->job_id;
	header.int_job_ref = in->job_id;
	memcpy(image->cccb, &header, sizeof(header));
	memcpy(image->cccb + sizeof(header), &command, sizeof(command));

	memset(&header, 0, sizeof(header));
	header.cmd_type = ROGUE_FWIF_CCB_CMD_TYPE_UPDATE;
	header.cmd_size = sizeof(update);
	update.addr = in->fw[A733_GPU_FWCOMPUTE_TIMELINE].addr;
	update.value = 1;
	memcpy(image->cccb + sizeof(header) + sizeof(command),
	    &header, sizeof(header));
	memcpy(image->cccb + 2 * sizeof(header) + sizeof(command),
	    &update, sizeof(update));

	image->used_bytes = A733_GPU_FWCOMPUTE_USED_BYTES;
	image->kick.cmd_type = ROGUE_FWIF_KCCB_CMD_KICK;
	kick = &image->kick.cmd_data.cmd_kick_data;
	kick->context_fw_addr = in->fw[A733_GPU_FWCOMPUTE_CONTEXT].addr;
	kick->client_woff_update = image->used_bytes;
	kick->client_wrap_mask_update = image->ctl.wrap_mask;
	return 0;
}
