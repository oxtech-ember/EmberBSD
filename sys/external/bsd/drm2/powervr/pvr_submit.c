/* SPDX-License-Identifier: MIT */
/* Origin: EmberBSD native PowerVR submission and error diagnostics. */
/* Copyright (c) 2023 Imagination Technologies Ltd. */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * AI-assisted native adaptation of the MIT alternative in Linux
 * 7b63ef2d55f24519e7e9e5f4d15dbea03f126e40: pvr_job.c and pvr_sync.c.
 * Native queued submission and retained wait references are EmberBSD changes.
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
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/kmem.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <sys/timearith.h>
#include <sys/mutex.h>
#include <drm/drm_sched_batch.h>
#include <sys/proc.h>
#include <linux/errno.h>
#include <linux/dma-fence.h>
#include <linux/dma-fence-chain.h>
#include <linux/slab.h>
#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <drm/drm_syncobj.h>
#include <arm/sunxi/powervr/pvr_rogue_fwif.h>
#include <arm/sunxi/powervr/pvr_rogue_fwif_client.h>
#include "pvr_backend.h"
#include "pvr_context.h"
#include "pvr_drm.h"
#include "pvr_exec.h"
#include "pvr_exec_hwrt.h"
#include "pvr_hwrt.h"
#include "pvr_file.h"
#include "pvr_stream.h"

/* GE8300 streams are copied into the buffers sized for the other profile. */
CTASSERT(PVR_STREAM_GE8300_TRANSFER_SIZE <= PVR_STREAM_TRANSFER_SIZE);
CTASSERT(PVR_STREAM_GE8300_GEOMETRY_SIZE <= PVR_STREAM_FRAGMENT_SIZE);

struct pvr_submit_job {
	union {
		struct rogue_fwif_cmd_compute compute;
		struct rogue_fwif_cmd_transfer transfer;
		struct rogue_fwif_cmd_geom geometry;
		struct rogue_fwif_cmd_frag fragment;
	} command;
	struct pvr_exec_context *context;
	struct pvr_exec_hwrt *hwrt;
	struct drm_sched_batch_item sched;
	uint32_t handle, id;
	uint32_t hwrt_handle;
	uint32_t type;
	uint8_t rt_index;
	uint32_t fragment_type;
	bool paired_fragment, vm_held;
	/* Held VM, recorded under the gate; the worker reads it while waiting. */
	struct pvr_file_vm *vm;
};
struct pvr_submit_batch {
	struct drm_sched_batch sched;
	struct pvr_submit_queue *queue;
	const struct pvr_backend *backend;
	size_t bytes;
	uint32_t count;
	struct pvr_submit_job jobs[];
};

struct pvr_submit_queue {
	struct drm_device *dev;
	struct drm_sched_batch_queue *scheduler;
};

static int pvr_submit_execute(void *, uint32_t);
static void pvr_submit_retire(void *);
static bool pvr_submit_cancelled(void *, uint32_t);
static bool pvr_submit_internal_dependency(void *, uint32_t, uint32_t);

static const struct drm_sched_batch_ops pvr_submit_ops = {
	.internal_dependency = pvr_submit_internal_dependency,
	.cancelled = pvr_submit_cancelled,
	.execute = pvr_submit_execute,
	.release = pvr_submit_retire,
};

int
pvr_submit_queue_create(struct drm_device *dev, struct pvr_submit_queue **out)
{
	struct pvr_submit_queue *queue;
	int error;

	queue = kmem_zalloc(sizeof(*queue), KM_SLEEP);
	queue->dev = dev;
	error = drm_sched_batch_queue_create("pvrsubmit", &queue->scheduler);
	if (error != 0) {
		kmem_free(queue, sizeof(*queue));
		return error;
	}
	*out = queue;
	return 0;
}

/* Attachment owns dev until reboot. A detach excludes new ioctl entrants and
 * quiesces callers not yet in reserve, then drains without either execution
 * gate held before dropping dev. See the common DRM batch lifetime contract. */
void
pvr_submit_queue_destroy(struct pvr_submit_queue *queue)
{

	if (queue == NULL)
		return;
	drm_sched_batch_queue_destroy(queue->scheduler);
	kmem_free(queue, sizeof(*queue));
}

/* Keep copied registers available before a failed batch is retained. */
static void
pvr_submit_error_command(const struct pvr_submit_job *job, int error)
{
	const void *command;
	size_t size, offset;
	uint32_t word;

	if (job->type == DRM_PVR_JOB_TYPE_GEOMETRY) {
		command = &job->command.geometry;
		size = sizeof(job->command.geometry);
	} else if (job->type == DRM_PVR_JOB_TYPE_FRAGMENT) {
		command = &job->command.fragment;
		size = sizeof(job->command.fragment);
	} else {
		return;
	}
	printf("PVR_SUBMIT_ERROR_CMD: job=%u type=%u context=%u "
	    "hwrt=%u rt=%u error=%d bytes=%zu", job->id, job->type,
	    job->handle, job->hwrt_handle, job->rt_index, error, size);
	/* Command words only; never dereference an embedded GPU address. */
	for (offset = 0; offset < size; offset += sizeof(word)) {
		memcpy(&word, (const uint8_t *)command + offset, sizeof(word));
		printf(" %08x", word);
	}
	printf("\n");
}

static int
pvr_submit_array(const struct drm_pvr_obj_array *array, size_t stride)
{
	uint64_t bytes;

	if (array->stride < stride)
		return EINVAL;
	if (array->stride > stride)
		return EOPNOTSUPP;
	if (array->count == 0)
		return 0;
	bytes = (uint64_t)array->count * stride;
	if (array->array == 0 || array->array > UINTPTR_MAX ||
	    bytes - 1 > UINTPTR_MAX - array->array)
		return EFAULT;
	return 0;
}

static int
pvr_submit_sync_valid(const struct drm_pvr_sync_op *input)
{
	uint32_t type;

	if ((input->flags & ~DRM_PVR_SYNC_OP_FLAGS_MASK) != 0)
		return EINVAL;
	type = input->flags & DRM_PVR_SYNC_OP_FLAG_HANDLE_TYPE_MASK;
	if (type != DRM_PVR_SYNC_OP_FLAG_HANDLE_TYPE_SYNCOBJ &&
	    type != DRM_PVR_SYNC_OP_FLAG_HANDLE_TYPE_TIMELINE_SYNCOBJ)
		return EINVAL;
	if (type == DRM_PVR_SYNC_OP_FLAG_HANDLE_TYPE_SYNCOBJ && input->value != 0)
		return EINVAL;
	return 0;
}

/* All user memory is copied and decoded before any firmware work starts. */
static int
pvr_submit_copy(const struct drm_pvr_obj_array *array,
    struct pvr_submit_batch *batch, uint32_t *index, uint64_t bvnc)
{
	struct drm_pvr_job input;
	struct drm_pvr_sync_op sync;
	struct pvr_submit_job *job;
	uint8_t stream[PVR_STREAM_COMPUTE_SIZE];
	uint64_t address;
	uint32_t i, j;
	int error;

	for (i = 0; i < batch->count; i++) {
		*index = i;
		job = &batch->jobs[i];
		address = array->array + (uint64_t)i * sizeof(input);
		error = copyin((const void *)(uintptr_t)address, &input, sizeof(input));
		if (error != 0)
			return error;
		job->type = input.type;
		job->handle = input.context_handle;
		job->hwrt_handle = input.hwrt.set_handle;
		job->rt_index = input.hwrt.data_index;
		if (job->type == DRM_PVR_JOB_TYPE_COMPUTE) {
			if (input.hwrt.set_handle != 0 || input.hwrt.data_index != 0 ||
			    input.cmd_stream == 0 || input.cmd_stream_len < sizeof(stream))
				return EINVAL;
			if (input.cmd_stream_len > sizeof(stream))
				return EOPNOTSUPP;
			if (input.cmd_stream > UINTPTR_MAX - (sizeof(stream) - 1))
				return EFAULT;
			error = copyin((const void *)(uintptr_t)input.cmd_stream,
			    stream, sizeof(stream));
			if (error != 0)
				return error;
			error = pvr_stream_compute(bvnc, stream,
			    sizeof(stream), input.flags, &job->command.compute);
			if (error != 0)
				return error;
		} else if (job->type == DRM_PVR_JOB_TYPE_TRANSFER_FRAG) {
			uint8_t transfer[PVR_STREAM_TRANSFER_SIZE];
			size_t expect = PVR_STREAM_TRANSFER_SIZE;

			if (bvnc == PVR_STREAM_GE8300_BVNC)
				expect = PVR_STREAM_GE8300_TRANSFER_SIZE;

			if (input.hwrt.set_handle != 0 || input.hwrt.data_index != 0 ||
			    input.cmd_stream == 0 || input.cmd_stream_len < expect)
				return EINVAL;
			if (input.cmd_stream_len > expect)
				return EOPNOTSUPP;
			if (input.cmd_stream > UINTPTR_MAX - (expect - 1))
				return EFAULT;
			error = copyin((const void *)(uintptr_t)input.cmd_stream,
			    transfer, expect);
			if (error != 0)
				return error;
			error = pvr_stream_transfer(bvnc, transfer,
			    expect, input.flags, &job->command.transfer);
			if (error != 0)
				return error;
		} else if (job->type == DRM_PVR_JOB_TYPE_GEOMETRY ||
		    job->type == DRM_PVR_JOB_TYPE_FRAGMENT) {
			uint8_t graphics[PVR_STREAM_FRAGMENT_SIZE];
			size_t expect = PVR_STREAM_FRAGMENT_SIZE;

			if (job->type == DRM_PVR_JOB_TYPE_GEOMETRY)
				expect = bvnc == PVR_STREAM_GE8300_BVNC ?
				    PVR_STREAM_GE8300_GEOMETRY_SIZE :
				    PVR_STREAM_GEOMETRY_SIZE;

			if (input.hwrt.set_handle == 0 || input.hwrt.data_index > 1 ||
			    input.cmd_stream == 0 || input.cmd_stream_len < expect)
				return EINVAL;
			if (input.cmd_stream_len > expect)
				return EOPNOTSUPP;
			if (input.cmd_stream > UINTPTR_MAX - (expect - 1))
				return EFAULT;
			error = copyin((const void *)(uintptr_t)input.cmd_stream,
			    graphics, expect);
			if (error != 0)
				return error;
			if (job->type == DRM_PVR_JOB_TYPE_GEOMETRY) {
				error = pvr_stream_geometry(bvnc,
				    graphics, expect, input.flags,
				    &job->command.geometry);
			} else {
				error = pvr_stream_fragment(bvnc,
				    graphics, expect, input.flags,
				    &job->command.fragment, &job->fragment_type);
			}
			if (error != 0)
				return error;

		} else {
			return EOPNOTSUPP;
		}
		error = pvr_submit_array(&input.sync_ops, sizeof(struct drm_pvr_sync_op));
		if (error != 0)
			return error;
		if (input.sync_ops.count == 0)
			continue;
		error = drm_sched_batch_sync_alloc(&batch->sched, i, input.sync_ops.count);
		if (error != 0)
			return error;
		for (j = 0; j < job->sched.sync_count; j++) {
			struct drm_sched_batch_sync *target = &job->sched.syncs[j];

			address = input.sync_ops.array + (uint64_t)j * sizeof(sync);
			error = copyin((const void *)(uintptr_t)address, &sync, sizeof(sync));
			if (error != 0)
				return error;
			error = pvr_submit_sync_valid(&sync);
			if (error != 0)
				return error;
			target->handle = sync.handle;
			target->value = sync.value;
			target->signal = (sync.flags & DRM_PVR_SYNC_OP_FLAG_SIGNAL) != 0;
			target->timeline = (sync.flags & DRM_PVR_SYNC_OP_FLAG_HANDLE_TYPE_MASK) ==
			    DRM_PVR_SYNC_OP_FLAG_HANDLE_TYPE_TIMELINE_SYNCOBJ;
		}
	}
	/* Pairing is checked only after every job decoded its type. */
	for (i = 0; i < batch->count; i++) {
		job = &batch->jobs[i];
		if (job->type == DRM_PVR_JOB_TYPE_GEOMETRY &&
		    (i + 1 >= batch->count ||
		    batch->jobs[i + 1].type != DRM_PVR_JOB_TYPE_FRAGMENT))
			return EOPNOTSUPP;
		if (job->type != DRM_PVR_JOB_TYPE_FRAGMENT)
			continue;
		if (i == 0)
			return EOPNOTSUPP;
		if (batch->jobs[i - 1].type == DRM_PVR_JOB_TYPE_GEOMETRY) {
			job->paired_fragment = true;
			job->sched.first = i - 1;
			continue;
		}
		/* Mesa may follow a paired PR with one final ordinary fragment. */
		if (!batch->jobs[i - 1].paired_fragment ||
		    batch->jobs[i - 1].fragment_type != ROGUE_FWIF_CCB_CMD_TYPE_FRAG_PR ||
		    job->fragment_type != ROGUE_FWIF_CCB_CMD_TYPE_FRAG)
			return EOPNOTSUPP;
	}
	return 0;
}

/* The gate keeps context records live until their references are acquired. */
static int
pvr_submit_references(struct drm_device *dev, struct drm_file *file,
    struct pvr_submit_batch *batch, uint32_t *index)
{
	struct pvr_submit_job *job;
	uint32_t i;
	int error;

	for (i = 0; i < batch->count; i++) {
		*index = i;
		job = &batch->jobs[i];
		job->context = pvr_exec_context_get(file, job->handle);
		if (job->context == NULL)
			return EINVAL;
		if (job->type == DRM_PVR_JOB_TYPE_COMPUTE) {
			if (pvr_context_type(pvr_exec_context_firmware(
			    job->context)) != DRM_PVR_CTX_TYPE_COMPUTE)
				return EINVAL;
		} else if (job->type == DRM_PVR_JOB_TYPE_TRANSFER_FRAG) {
			if (pvr_context_type(pvr_exec_context_firmware(
			    job->context)) != DRM_PVR_CTX_TYPE_TRANSFER_FRAG)
				return EINVAL;
		} else {
			if (pvr_context_type(pvr_exec_context_firmware(
			    job->context)) != DRM_PVR_CTX_TYPE_RENDER)
				return EINVAL;
			if (job->type == DRM_PVR_JOB_TYPE_FRAGMENT) {
				const struct pvr_submit_job *previous = &batch->jobs[i - 1];

				if (job->hwrt_handle != previous->hwrt_handle ||
				    job->rt_index != previous->rt_index ||
				    job->handle != previous->handle)
					return EINVAL;
				/* Both fragments borrow the geometry owner reference. */
				job->hwrt = batch->jobs[i - 1].hwrt;
				if (job->hwrt == NULL)
					return EINVAL;
			} else {
				job->hwrt = pvr_exec_hwrt_get(file,
				    job->hwrt_handle);
				if (job->hwrt == NULL)
					return EINVAL;
			}
			/* The dataset, the context and every job share one VM. */
			if (pvr_exec_hwrt_vm(job->hwrt) !=
			    pvr_exec_context_vm(job->context))
				return EINVAL;
		}
		error = pvr_device_next_id(dev, &job->id);
		if (error != 0)
			return error;
	}
	return drm_sched_batch_prepare(&batch->sched, file);
}

static void
pvr_submit_release(struct drm_device *dev, struct pvr_submit_batch *batch,
    bool gated)
{
	struct pvr_submit_job *job;
	uint32_t i;

	drm_sched_batch_cleanup(&batch->sched);
	for (i = 0; i < batch->count; i++) {
		job = &batch->jobs[i];
		if (job->context != NULL) {
			KASSERT(gated);
			if (job->vm_held)
				pvr_file_vm_submit_drop(pvr_exec_context_vm(job->context));
			pvr_exec_context_put(dev, job->context);
		}
		if (job->hwrt != NULL && job->type != DRM_PVR_JOB_TYPE_FRAGMENT) {
			KASSERT(gated);
			pvr_exec_hwrt_put(dev, job->hwrt);
		}
	}
	kmem_free(batch, batch->bytes);
}

/* Called only between completion probes, with retained context/VM/mappings. */
static void
pvr_submit_wait_gate(void *cookie, bool enter)
{
	struct pvr_submit_batch *batch = cookie;

	if (enter) {
		const struct pvr_backend *backend = pvr_device_enter(batch->queue->dev);
		KASSERT(backend == batch->backend); /* Installed once, alive until reboot. */
	} else {
		pvr_device_leave(batch->queue->dev);
	}
}

static bool
pvr_submit_internal_dependency(void *cookie, uint32_t index, uint32_t producer)
{
	struct pvr_submit_batch *batch = cookie;

	/* Only the adjacent paired geometry signal is encoded as FENCE_PR. */
	return batch->jobs[index].paired_fragment && producer + 1 == index;
}

static bool
pvr_submit_cancelled(void *cookie, uint32_t index)
{
	struct pvr_submit_batch *batch = cookie;
	struct pvr_file_vm *vm = batch->jobs[index].vm;

	return vm != NULL && pvr_file_vm_closed(vm);
}

static void
pvr_submit_retire(void *cookie)
{
	struct pvr_submit_batch *batch = cookie;
	struct drm_device *dev = batch->queue->dev;

	(void)pvr_device_enter(dev);
	pvr_submit_release(dev, batch, true);
	pvr_device_leave(dev);
}

static int
pvr_submit_execute(void *cookie, uint32_t index)
{
	struct pvr_submit_batch *batch = cookie;
	struct pvr_submit_queue *queue = batch->queue;
	struct drm_device *dev = queue->dev;
	const struct pvr_backend *backend = batch->backend;
	struct pvr_submit_job *job = &batch->jobs[index];
	struct pvr_file_vm *vm;
	int error = 0;

	(void)pvr_device_enter(dev);
	vm = pvr_exec_context_vm(job->context);
	/* Work of a closed file has no consumer; do not run it. */
	if (error == 0 && (drm_sched_batch_stopping(queue->scheduler) ||
	    pvr_file_vm_closed(vm)))
		error = ECANCELED;
	if (error == 0)
		error = backend->check(backend->cookie);
	if (error == 0)
		error = pvr_file_vm_prepare(vm);
	if (error != 0) {
		pvr_device_leave(dev);
		return error;
	}
	pvr_context_wait_gate(pvr_exec_context_firmware(job->context),
	    pvr_submit_wait_gate, batch);
	pvr_device_note_job(dev, vm, pvr_exec_context_id(job->context),
	    job->id, job->type);
	if (job->type == DRM_PVR_JOB_TYPE_COMPUTE) {
		error = pvr_context_compute(
		    pvr_exec_context_firmware(job->context),
		    &job->command.compute, job->id);
	} else if (job->type == DRM_PVR_JOB_TYPE_TRANSFER_FRAG) {
		error = pvr_context_transfer(
		    pvr_exec_context_firmware(job->context),
		    &job->command.transfer, job->id);
	} else if (job->type == DRM_PVR_JOB_TYPE_FRAGMENT) {
		struct pvr_hwrt_target target;

		error = pvr_exec_hwrt_target(job->hwrt, job->rt_index,
		    &target);
		if (error == 0) {
			job->command.fragment.cmd_shared.
			    hwrt_data_fw_addr = target.fw_address;
			error = pvr_context_fragment(
			    pvr_exec_context_firmware(job->context),
			    &job->command.fragment, target.cleanup_address,
			    job->id);
		}
	} else {
		struct pvr_submit_job *frag = &batch->jobs[index + 1];
		struct pvr_hwrt_target target;

		/* Mark the render target exposed immediately before
		 * execution; both commands share its firmware data. */
		error = pvr_exec_hwrt_target(job->hwrt, job->rt_index,
		    &target);
		if (error == 0) {
			job->command.geometry.cmd_shared.
			    hwrt_data_fw_addr = target.fw_address;
			frag->command.fragment.cmd_shared.
			    hwrt_data_fw_addr = target.fw_address;
			if (job->id == 0 || frag->id == 0)
				error = EINVAL;
		}
		if (error == 0)
			pvr_device_note_job(dev, vm,
			    pvr_exec_context_id(frag->context), frag->id, frag->type);
		if (error == 0)
			error = pvr_context_render_pair(
			    pvr_exec_context_firmware(job->context),
			    &job->command.geometry,
			    &frag->command.fragment,
			    frag->fragment_type, target.cleanup_address,
			    job->id, frag->id);
	}
	pvr_context_wait_gate(pvr_exec_context_firmware(job->context), NULL, NULL);
	if (error != 0 &&
	    pvr_context_lost(pvr_exec_context_firmware(job->context)) &&
	    !pvr_context_retained(pvr_exec_context_firmware(job->context))) {
		/* Firmware recovered, reset and drained only this context.
		 * Its file sees EIO; other files and contexts keep running. */
		pvr_submit_error_command(job, error);
		pvr_file_vm_lose(vm);
		pvr_file_vm_complete(vm);
	} else if (error != 0) {
		/* An error is not proof that DMA stopped. VM retirement quarantines
		 * all tables and mapped BOs until acknowledged device recovery. */
		pvr_submit_error_command(job, error);
		pvr_file_vm_fail(vm);
	} else {
		pvr_file_vm_complete(vm);
	}
	pvr_device_leave(dev);
	return error;
}

int
pvr_submit_jobs(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_pvr_ioctl_submit_jobs_args *args = data;
	struct drm_pvr_obj_array input = args->jobs;
	struct pvr_submit_queue *queue = pvr_device_submit(dev);
	struct pvr_submit_batch *batch;
	const struct pvr_backend *backend;
	struct timespec now, deadline;
	size_t bytes;
	uint32_t index = 0;
	int error, paused;

	args->jobs.count = 0;
	error = drm_sched_batch_size(input.count, sizeof(batch->jobs[0]),
	    sizeof(*batch), &bytes);
	if (error != 0)
		return -error;
	error = pvr_submit_array(&input, sizeof(struct drm_pvr_job));
	if (error != 0)
		return -error;
	error = drm_sched_batch_reserve(queue->scheduler);
	if (error != 0)
		return -error;
	batch = kmem_zalloc(bytes, KM_NOSLEEP);
	if (batch == NULL) {
		drm_sched_batch_unreserve(queue->scheduler);
		return -ENOMEM;
	}
	batch->queue = queue;
	batch->bytes = bytes;
	batch->count = input.count;
	error = drm_sched_batch_init(&batch->sched, queue->scheduler,
	    &batch->jobs[0].sched, sizeof(batch->jobs[0]), input.count, bytes,
	    &pvr_submit_ops, batch);
	if (error != 0) {
		kmem_free(batch, bytes);
		drm_sched_batch_unreserve(queue->scheduler);
		return -error;
	}
	error = pvr_submit_copy(&input, batch, &index, pvr_device_bvnc(dev));
	if (error != 0) {
		pvr_submit_release(dev, batch, false);
		drm_sched_batch_unreserve(queue->scheduler);
		return -error;
	}
	backend = pvr_device_enter(dev);
	batch->backend = backend;
	if (backend == NULL) {
		error = EOPNOTSUPP;
		goto out;
	}
	error = pvr_submit_references(dev, file, batch, &index);
	if (error != 0)
		goto out;
	pvr_device_leave(dev);
	error = drm_sched_batch_capture(&batch->sched);
	if (pvr_device_enter(dev) != backend) {
		/* A backend owner must never change while retained contexts exist. */
		pvr_device_retain(dev, batch);
		pvr_device_leave(dev);
		drm_sched_batch_unreserve(queue->scheduler);
		return -EIO;
	}
	if (error != 0)
		goto out;
	if (drm_sched_batch_stopping(queue->scheduler)) {
		error = ENXIO;
		goto out;
	}
	drm_sched_batch_deadline(&deadline);
	for (;;) {
		for (index = 0; index < batch->count; index++) {
			struct pvr_submit_job *job = &batch->jobs[index];

			job->vm = pvr_exec_context_vm(job->context);
			error = pvr_file_vm_submit_hold(job->vm);
			if (error != 0)
				break;
			job->vm_held = true;
		}
		if (error != EAGAIN)
			break;
		/* A waiting MAP/UNMAP drains this VM first. Wait holding nothing. */
		while (index-- > 0) {
			pvr_file_vm_submit_drop(
			    pvr_exec_context_vm(batch->jobs[index].context));
			batch->jobs[index].vm_held = false;
		}
		pvr_device_leave(dev);
		paused = kpause("pvrvmbar", true, 1, NULL);
		getnanouptime(&now);
		if (pvr_device_enter(dev) != backend) {
			pvr_device_retain(dev, batch);
			pvr_device_leave(dev);
			drm_sched_batch_unreserve(queue->scheduler);
			return -EIO;
		}
		if (paused != 0 && paused != EWOULDBLOCK) {
			error = paused == ERESTART ? EINTR : paused;
			goto out;
		}
		if (timespeccmp(&now, &deadline, >=)) {
			error = ETIMEDOUT;
			goto out;
		}
		if (drm_sched_batch_stopping(queue->scheduler)) {
			error = ENXIO;
			goto out;
		}
	}
	if (error != 0)
		goto out;
	/* Publication and enqueue have one order under the device gate. Another
	 * submit may capture these fences but cannot enqueue ahead of this batch. */
	error = drm_sched_batch_commit(&batch->sched);
	if (error != 0)
		goto out;
	pvr_device_leave(dev);
	args->jobs.count = input.count;
	return 0;
out:
	pvr_submit_release(dev, batch, true);
	pvr_device_leave(dev);
	drm_sched_batch_unreserve(queue->scheduler);
	return -error;
}
