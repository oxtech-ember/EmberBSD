/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * AI-assisted native adaptation of the MIT alternative in Linux
 * 7b63ef2d55f24519e7e9e5f4d15dbea03f126e40: pvr_context.c, pvr_queue.c
 * and pvr_cccb.c. Firmware structures retain their original ABI and authors.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/kmem.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <arm/sunxi/powervr/pvr_rogue_fwif.h>
#include <arm/sunxi/powervr/pvr_rogue_fwif_client.h>
#include "pvr_backend.h"
#include "pvr_context.h"
#include "pvr_drm.h"
#include "pvr_stream.h"

#define PVR_CONTEXT_RING_SIZE (1U << 15)
#define PVR_CONTEXT_POLL_COUNT 1000

struct pvr_context_queue {
	struct pvr_fw_object ring, control, state, timeline;
	uint32_t offset, written, sequence;
	bool exposed, cleaned;
};
struct pvr_context {
	const struct pvr_backend *backend;
	struct pvr_fw_object firmware;
	struct pvr_context_queue queue[2];
	uint32_t type, count;
	bool failed;
};
union pvr_context_image {
	struct rogue_fwif_fwcomputecontext compute;
	struct rogue_fwif_fwrendercontext render;
	struct rogue_fwif_fwtransfercontext transfer;
};

static int
pvr_context_queue_init(struct pvr_context *ctx, unsigned index,
    const struct pvr_context_config *config)
{
	const struct pvr_backend *be = ctx->backend;
	struct pvr_context_queue *queue = &ctx->queue[index];
	struct rogue_fwif_fwcommoncontext *common;
	struct rogue_fwif_cccb_ctl *control;
	int error;

	error = be->alloc(be->cookie, PVR_CONTEXT_RING_SIZE, &queue->ring);
	if (error != 0)
		return error;
	error = be->alloc(be->cookie, sizeof(*control), &queue->control);
	if (error != 0)
		return error;
	/* One page covers all register state for the fixed BXM profile. */
	error = be->alloc(be->cookie, 4096, &queue->state);
	if (error != 0)
		return error;
	error = be->alloc(be->cookie, sizeof(uint32_t), &queue->timeline);
	if (error != 0)
		return error;
	if (ctx->type == DRM_PVR_CTX_TYPE_RENDER) {
		queue->offset = index == 0 ?
		    offsetof(struct rogue_fwif_fwrendercontext, geom_context) :
		    offsetof(struct rogue_fwif_fwrendercontext, frag_context);
		if (index == 0) {
			struct rogue_fwif_geom_ctx_state *state = queue->state.cpu;
			state->geom_core[0].geom_reg_vdm_call_stack_pointer_init =
			    config->callstack;
		}
	}
	common = (void *)((uint8_t *)ctx->firmware.cpu + queue->offset);
	common->ccbctl_fw_addr = queue->control.address;
	common->ccb_fw_addr = queue->ring.address;
	common->context_state_addr = queue->state.address;
	common->fw_mem_context_fw_addr = config->vm_address;
	common->dm = ctx->type == DRM_PVR_CTX_TYPE_COMPUTE ? PVR_FWIF_DM_CDM :
	    ctx->type == DRM_PVR_CTX_TYPE_RENDER && index == 0 ?
	    PVR_FWIF_DM_GEOM : PVR_FWIF_DM_FRAG;
	common->priority = config->priority;
	common->max_deadline_ms = 30000;
	common->server_common_context_id = config->id;
	common->pid = config->pid;
	control = queue->control.cpu;
	control->wrap_mask = PVR_CONTEXT_RING_SIZE - 1;
	be->sync(be->cookie, &queue->control,
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
	be->sync(be->cookie, &queue->state,
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
	be->sync(be->cookie, &queue->timeline,
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
	return be->check(be->cookie);
}

int
pvr_context_create(const struct pvr_backend *be,
    const struct pvr_context_config *config, struct pvr_context **owner)
{
	union pvr_context_image image;
	struct pvr_context *ctx;
	size_t size;
	unsigned i;
	int error;

	if (be == NULL || config == NULL || owner == NULL || *owner != NULL ||
	    config->priority > 2 || config->id == 0 ||
	    config->vm_address == 0 || (config->vm_address & 7) != 0)
		return EINVAL;
	memset(&image, 0, sizeof(image));
	switch (config->type) {
	case DRM_PVR_CTX_TYPE_RENDER:
		size = sizeof(image.render);
		error = pvr_stream_static_render(PVR_STREAM_BVNC,
		    config->static_state, config->static_size,
		    &image.render.static_render_context_state);
		break;
	case DRM_PVR_CTX_TYPE_COMPUTE:
		if (config->callstack != 0)
			return EINVAL;
		size = sizeof(image.compute);
		error = pvr_stream_static_compute(PVR_STREAM_BVNC,
		    config->static_state, config->static_size,
		    &image.compute.static_compute_context_state.ctxswitch_regs);
		break;
	case DRM_PVR_CTX_TYPE_TRANSFER_FRAG:
		if (config->callstack != 0)
			return EINVAL;
		size = sizeof(image.transfer);
		error = pvr_stream_static_transfer(PVR_STREAM_BVNC,
		    config->static_state, config->static_size);
		break;
	default:
		return EINVAL;
	}
	if (error != 0)
		return error;
	error = be->check(be->cookie);
	if (error != 0)
		return error;
	ctx = kmem_zalloc(sizeof(*ctx), KM_SLEEP);
	ctx->backend = be;
	ctx->type = config->type;
	ctx->count = config->type == DRM_PVR_CTX_TYPE_RENDER ? 2 : 1;
	*owner = ctx;
	error = be->alloc(be->cookie, size, &ctx->firmware);
	if (error != 0)
		return error;
	memcpy(ctx->firmware.cpu, &image, size);
	for (i = 0; i < ctx->count; i++) {
		error = pvr_context_queue_init(ctx, i, config);
		if (error != 0)
			return error;
	}
	be->sync(be->cookie, &ctx->firmware,
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
	return be->check(be->cookie);
}

int
pvr_context_destroy(struct pvr_context **owner)
{
	struct pvr_context *ctx;
	struct pvr_context_queue *queue;
	const struct pvr_backend *be;
	struct rogue_fwif_kccb_cmd command;
	struct pvr_fw_object *objects[4];
	uint32_t result;
	unsigned i, j;
	int error;

	if (owner == NULL || *owner == NULL)
		return EINVAL;
	ctx = *owner;
	be = ctx->backend;
	if (ctx->failed)
		return EIO;
	error = be->check(be->cookie);
	if (error != 0)
		return error;
	/* Retire every exposed queue before removing any context backing. */
	for (i = 0; i < ctx->count; i++) {
		queue = &ctx->queue[i];
		if (!queue->exposed || queue->cleaned)
			continue;
		memset(&command, 0, sizeof(command));
		command.cmd_type = ROGUE_FWIF_KCCB_CMD_CLEANUP;
		command.cmd_data.cleanup_data.cleanup_type =
		    ROGUE_FWIF_CLEANUP_FWCOMMONCONTEXT;
		command.cmd_data.cleanup_data.cleanup_data.context_fw_addr =
		    ctx->firmware.address + queue->offset;
		error = be->command(be->cookie, &command, &result);
		if (error != 0)
			return error;
		if (result != ROGUE_FWIF_KCCB_RTN_SLOT_CMD_EXECUTED) {
			be->fail(be->cookie);
			return EIO;
		}
		queue->cleaned = true;
	}
	for (i = 0; i < ctx->count; i++) {
		queue = &ctx->queue[i];
		objects[0] = &queue->timeline;
		objects[1] = &queue->state;
		objects[2] = &queue->control;
		objects[3] = &queue->ring;
		for (j = 0; j < __arraycount(objects); j++) {
			if (objects[j]->owner == NULL)
				continue;
			error = be->free(be->cookie, objects[j]);
			if (error != 0)
				return error;
		}
	}
	if (ctx->firmware.owner != NULL) {
		error = be->free(be->cookie, &ctx->firmware);
		if (error != 0)
			return error;
	}
	kmem_free(ctx, sizeof(*ctx));
	*owner = NULL;
	return 0;
}

static void
pvr_context_write(struct pvr_context_queue *queue, uint32_t type,
    const void *data, size_t size, uint32_t id)
{
	struct rogue_fwif_ccb_cmd_header header;
	uint8_t *ring = queue->ring.cpu;

	memset(&header, 0, sizeof(header));
	header.cmd_type = type;
	header.cmd_size = (uint32_t)roundup2(size, 8);
	header.ext_job_ref = header.int_job_ref = id;
	memcpy(ring + queue->written, &header, sizeof(header));
	memcpy(ring + queue->written + sizeof(header), data, size);
	queue->written += sizeof(header) + roundup2(size, 8);
}

int
pvr_context_compute(struct pvr_context *ctx,
    const struct rogue_fwif_cmd_compute *command, uint32_t id)
{
	const struct pvr_backend *be;
	struct pvr_context_queue *queue;
	struct rogue_fwif_cccb_ctl *control;
	struct rogue_fwif_ccb_cmd_header padding;
	struct rogue_fwif_kccb_cmd kick;
	struct rogue_fwif_ufo update;
	uint32_t read, value, result;
	size_t used, remaining;
	unsigned i;
	int error;

	if (ctx == NULL || command == NULL || id == 0)
		return EINVAL;
	if (ctx->type != DRM_PVR_CTX_TYPE_COMPUTE)
		return EOPNOTSUPP;
	if (ctx->failed)
		return EIO;
	be = ctx->backend;
	queue = &ctx->queue[0];
	if (queue->cleaned || queue->sequence == UINT32_MAX)
		return EOVERFLOW;
	error = be->check(be->cookie);
	if (error != 0)
		return error;
	control = queue->control.cpu;
	be->sync(be->cookie, &queue->control, BUS_DMASYNC_POSTREAD);
	read = control->read_offset;
	be->sync(be->cookie, &queue->control, BUS_DMASYNC_PREREAD);
	/* This first native scheduler has one synchronous job per device. */
	if (read != queue->written) {
		error = EIO;
		goto failed;
	}
	used = 2 * sizeof(padding) + sizeof(*command) + sizeof(update);
	remaining = PVR_CONTEXT_RING_SIZE - queue->written;
	if (remaining < used + sizeof(padding)) {
		memset(&padding, 0, sizeof(padding));
		padding.cmd_type = ROGUE_FWIF_CCB_CMD_TYPE_PADDING;
		padding.cmd_size = (uint32_t)(remaining - sizeof(padding));
		memcpy((uint8_t *)queue->ring.cpu + queue->written,
		    &padding, sizeof(padding));
		queue->written = 0;
	}
	pvr_context_write(queue, ROGUE_FWIF_CCB_CMD_TYPE_CDM,
	    command, sizeof(*command), id);
	update.addr = queue->timeline.address;
	update.value = ++queue->sequence;
	pvr_context_write(queue, ROGUE_FWIF_CCB_CMD_TYPE_UPDATE,
	    &update, sizeof(update), id);
	be->sync(be->cookie, &queue->ring, BUS_DMASYNC_PREWRITE);
	memset(&kick, 0, sizeof(kick));
	kick.cmd_type = ROGUE_FWIF_KCCB_CMD_KICK;
	kick.cmd_data.cmd_kick_data.context_fw_addr = ctx->firmware.address;
	kick.cmd_data.cmd_kick_data.client_woff_update = queue->written;
	kick.cmd_data.cmd_kick_data.client_wrap_mask_update =
	    PVR_CONTEXT_RING_SIZE - 1;
	queue->exposed = true;
	error = be->command(be->cookie, &kick, &result);
	if (error != 0)
		goto failed;
	if (result != ROGUE_FWIF_KCCB_RTN_SLOT_CMD_EXECUTED) {
		error = EIO;
		goto failed;
	}
	for (i = 0; i < PVR_CONTEXT_POLL_COUNT; i++) {
		be->sync(be->cookie, &queue->timeline, BUS_DMASYNC_POSTREAD);
		value = *(const uint32_t *)queue->timeline.cpu;
		be->sync(be->cookie, &queue->timeline, BUS_DMASYNC_PREREAD);
		be->sync(be->cookie, &queue->control, BUS_DMASYNC_POSTREAD);
		read = control->read_offset;
		be->sync(be->cookie, &queue->control, BUS_DMASYNC_PREREAD);
		if (value == queue->sequence && read == queue->written) {
			error = be->check(be->cookie);
			if (error != 0)
				goto failed;
			be->sync(be->cookie, &queue->ring, BUS_DMASYNC_POSTWRITE);
			return 0;
		}
		if (i % 10 == 0) {
			error = be->check(be->cookie);
			if (error != 0)
				goto failed;
		}
		(void)kpause("pvrjob", false, MAX(1, hz / 100), NULL);
	}
	error = ETIMEDOUT;
failed:
	ctx->failed = true;
	be->fail(be->cookie);
	return error;
}

/* The caller validates both queues before publishing either half of a pair. */
static int
pvr_context_queue_idle(struct pvr_context *ctx, unsigned index)
{
	const struct pvr_backend *be = ctx->backend;
	struct pvr_context_queue *queue = &ctx->queue[index];
	struct rogue_fwif_cccb_ctl *control = queue->control.cpu;
	uint32_t read;

	if (queue->cleaned || queue->sequence == UINT32_MAX)
		return EOVERFLOW;
	be->sync(be->cookie, &queue->control, BUS_DMASYNC_POSTREAD);
	read = control->read_offset;
	be->sync(be->cookie, &queue->control, BUS_DMASYNC_PREREAD);
	return read == queue->written ? 0 : EIO;
}

static void
pvr_context_graphics_write(struct pvr_context *ctx, unsigned index,
    uint32_t type, const void *data, size_t size, uint32_t id,
    const struct rogue_fwif_ufo *dependency)
{
	const struct pvr_backend *be = ctx->backend;
	struct pvr_context_queue *queue = &ctx->queue[index];
	struct rogue_fwif_ccb_cmd_header padding;
	struct rogue_fwif_ufo update;
	size_t used, remaining;

	used = 2 * sizeof(padding) + roundup2(size, 8) + sizeof(update);
	if (dependency != NULL)
		used += sizeof(padding) + sizeof(*dependency);
	remaining = PVR_CONTEXT_RING_SIZE - queue->written;
	if (remaining < used + sizeof(padding)) {
		memset(&padding, 0, sizeof(padding));
		padding.cmd_type = ROGUE_FWIF_CCB_CMD_TYPE_PADDING;
		padding.cmd_size = (uint32_t)(remaining - sizeof(padding));
		memcpy((uint8_t *)queue->ring.cpu + queue->written,
		    &padding, sizeof(padding));
		queue->written = 0;
	}
	if (dependency != NULL)
		pvr_context_write(queue, ROGUE_FWIF_CCB_CMD_TYPE_FENCE_PR,
		    dependency, sizeof(*dependency), 0);
	pvr_context_write(queue, type, data, size, id);
	update.addr = queue->timeline.address;
	update.value = ++queue->sequence;
	pvr_context_write(queue, ROGUE_FWIF_CCB_CMD_TYPE_UPDATE,
	    &update, sizeof(update), 0);
	be->sync(be->cookie, &queue->ring, BUS_DMASYNC_PREWRITE);
}

static void
pvr_context_graphics_kick(struct pvr_context *ctx, unsigned index,
    uint32_t cleanup, struct rogue_fwif_kccb_cmd_kick_data *kick)
{
	struct pvr_context_queue *queue = &ctx->queue[index];

	kick->context_fw_addr = ctx->firmware.address + queue->offset;
	kick->client_woff_update = queue->written;
	kick->client_wrap_mask_update = PVR_CONTEXT_RING_SIZE - 1;
	if (cleanup != 0) {
		kick->num_cleanup_ctl = 1;
		kick->cleanup_ctl_fw_addr[0] = cleanup;
	}
	queue->exposed = true;
}

/* Wait for all queues together. Geometry may need a partial render to finish. */
static int
pvr_context_graphics_wait(struct pvr_context *ctx, unsigned mask)
{
	const struct pvr_backend *be = ctx->backend;
	struct pvr_context_queue *queue;
	struct rogue_fwif_cccb_ctl *control;
	uint32_t read, value;
	unsigned i, j, done = 0;
	int error;

	for (i = 0; i < PVR_CONTEXT_POLL_COUNT; i++) {
		for (j = 0; j < ctx->count; j++) {
			if ((mask & (1U << j)) == 0)
				continue;
			queue = &ctx->queue[j];
			control = queue->control.cpu;
			be->sync(be->cookie, &queue->timeline, BUS_DMASYNC_POSTREAD);
			value = *(const uint32_t *)queue->timeline.cpu;
			be->sync(be->cookie, &queue->timeline, BUS_DMASYNC_PREREAD);
			be->sync(be->cookie, &queue->control, BUS_DMASYNC_POSTREAD);
			read = control->read_offset;
			be->sync(be->cookie, &queue->control, BUS_DMASYNC_PREREAD);
			if (value == queue->sequence && read == queue->written)
				done |= 1U << j;
		}
		if (done == mask) {
			error = be->check(be->cookie);
			if (error != 0)
				return error;
			for (j = 0; j < ctx->count; j++)
				if ((mask & (1U << j)) != 0)
					be->sync(be->cookie, &ctx->queue[j].ring,
					    BUS_DMASYNC_POSTWRITE);
			return 0;
		}
		if (i % 10 == 0) {
			error = be->check(be->cookie);
			if (error != 0)
				return error;
		}
		(void)kpause("pvrjob", false, MAX(1, hz / 100), NULL);
	}
	return ETIMEDOUT;
}

static int
pvr_context_graphics_send(struct pvr_context *ctx,
    const struct rogue_fwif_kccb_cmd *kick, unsigned mask)
{
	const struct pvr_backend *be = ctx->backend;
	uint32_t result;
	int error;

	error = be->command(be->cookie, kick, &result);
	if (error == 0 && result != ROGUE_FWIF_KCCB_RTN_SLOT_CMD_EXECUTED)
		error = EIO;
	if (error == 0)
		error = pvr_context_graphics_wait(ctx, mask);
	if (error != 0) {
		ctx->failed = true;
		be->fail(be->cookie);
	}
	return error;
}

int
pvr_context_render_pair(struct pvr_context *ctx,
    const struct rogue_fwif_cmd_geom *geometry,
    const struct rogue_fwif_cmd_frag *fragment, uint32_t fragment_type,
    uint32_t cleanup, uint32_t geometry_id, uint32_t fragment_id)
{
	struct rogue_fwif_cmd_geom command;
	struct rogue_fwif_kccb_cmd kick;
	struct rogue_fwif_ufo dependency;
	int error;

	if (ctx == NULL || geometry == NULL || fragment == NULL ||
	    cleanup == 0 || (cleanup & 63) != 0 ||
	    geometry->cmd_shared.hwrt_data_fw_addr == 0 ||
	    geometry->cmd_shared.hwrt_data_fw_addr != fragment->cmd_shared.hwrt_data_fw_addr ||
	    geometry_id == 0 || fragment_id == 0)
		return EINVAL;
	if (ctx->type != DRM_PVR_CTX_TYPE_RENDER ||
	    (fragment_type != ROGUE_FWIF_CCB_CMD_TYPE_FRAG &&
	    fragment_type != ROGUE_FWIF_CCB_CMD_TYPE_FRAG_PR))
		return EOPNOTSUPP;
	if (ctx->failed)
		return EIO;
	error = ctx->backend->check(ctx->backend->cookie);
	if (error == 0)
		error = pvr_context_queue_idle(ctx, 0);
	if (error == 0)
		error = pvr_context_queue_idle(ctx, 1);
	if (error != 0)
		return error;
	command = *geometry;
	command.partial_render_geom_frag_fence.addr = ctx->queue[0].timeline.address;
	command.partial_render_geom_frag_fence.value = ctx->queue[0].sequence;
	pvr_context_graphics_write(ctx, 0, ROGUE_FWIF_CCB_CMD_TYPE_GEOM,
	    &command, sizeof(command), geometry_id, NULL);
	dependency.addr = ctx->queue[0].timeline.address;
	dependency.value = ctx->queue[0].sequence;
	pvr_context_graphics_write(ctx, 1, fragment_type,
	    fragment, sizeof(*fragment), fragment_id, &dependency);
	memset(&kick, 0, sizeof(kick));
	kick.cmd_type = ROGUE_FWIF_KCCB_CMD_COMBINED_GEOM_FRAG_KICK;
	pvr_context_graphics_kick(ctx, 0, cleanup,
	    &kick.cmd_data.combined_geom_frag_cmd_kick_data.geom_cmd_kick_data);
	/* A partial render uses the resources already held by geometry. */
	pvr_context_graphics_kick(ctx, 1,
	    fragment_type == ROGUE_FWIF_CCB_CMD_TYPE_FRAG_PR ? 0 : cleanup,
	    &kick.cmd_data.combined_geom_frag_cmd_kick_data.frag_cmd_kick_data);
	return pvr_context_graphics_send(ctx, &kick, 3);
}

static int
pvr_context_graphics_single(struct pvr_context *ctx, unsigned index,
    uint32_t type, const void *command, size_t size, uint32_t cleanup, uint32_t id)
{
	struct rogue_fwif_kccb_cmd kick;
	int error;

	if (ctx->failed)
		return EIO;
	error = ctx->backend->check(ctx->backend->cookie);
	if (error == 0)
		error = pvr_context_queue_idle(ctx, index);
	if (error != 0)
		return error;
	pvr_context_graphics_write(ctx, index, type, command, size, id, NULL);
	memset(&kick, 0, sizeof(kick));
	kick.cmd_type = ROGUE_FWIF_KCCB_CMD_KICK;
	pvr_context_graphics_kick(ctx, index, cleanup, &kick.cmd_data.cmd_kick_data);
	return pvr_context_graphics_send(ctx, &kick, 1U << index);
}

int
pvr_context_fragment(struct pvr_context *ctx,
    const struct rogue_fwif_cmd_frag *command, uint32_t cleanup, uint32_t id)
{

	if (ctx == NULL || command == NULL || cleanup == 0 || (cleanup & 63) != 0 ||
	    command->cmd_shared.hwrt_data_fw_addr == 0 || id == 0)
		return EINVAL;
	if (ctx->type != DRM_PVR_CTX_TYPE_RENDER)
		return EOPNOTSUPP;
	return pvr_context_graphics_single(ctx, 1, ROGUE_FWIF_CCB_CMD_TYPE_FRAG,
	    command, sizeof(*command), cleanup, id);
}

int
pvr_context_transfer(struct pvr_context *ctx,
    const struct rogue_fwif_cmd_transfer *command, uint32_t id)
{

	if (ctx == NULL || command == NULL || id == 0)
		return EINVAL;
	if (ctx->type != DRM_PVR_CTX_TYPE_TRANSFER_FRAG)
		return EOPNOTSUPP;
	return pvr_context_graphics_single(ctx, 0, ROGUE_FWIF_CCB_CMD_TYPE_TQ_3D,
	    command, sizeof(*command), 0, id);
}

uint32_t
pvr_context_type(const struct pvr_context *ctx)
{

	return ctx->type;
}
