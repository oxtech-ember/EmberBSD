/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * AI-assisted native adaptation of the MIT alternative in Linux
 * 7b63ef2d55f24519e7e9e5f4d15dbea03f126e40: pvr_job.c and pvr_sync.c.
 * Native synchronous submission and retained wait references are EmberBSD changes.
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
#include <linux/errno.h>
#include <linux/dma-fence.h>
#include <linux/dma-fence-chain.h>
#include <linux/slab.h>
#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <drm/drm_syncobj.h>
#include <arm/sunxi/powervr/pvr_rogue_fwif_client.h>
#include "pvr_backend.h"
#include "pvr_context.h"
#include "pvr_drm.h"
#include "pvr_exec.h"
#include "pvr_file.h"
#include "pvr_stream.h"

/* Policy for this synchronous slice, not a firmware or UAPI size limit. */
#define PVR_SUBMIT_WAIT_SECONDS 10

struct pvr_submit_sync {
	struct drm_pvr_sync_op input;
	struct drm_syncobj *object;
	struct dma_fence_chain *chain;
};
struct pvr_submit_job {
	struct rogue_fwif_cmd_compute command;
	struct pvr_exec_context *context;
	struct pvr_submit_sync *syncs;
	uint32_t handle, sync_count, id;
};
struct pvr_submit_batch {
	size_t bytes;
	uint32_t count;
	struct pvr_submit_job jobs[];
};

static int
pvr_submit_bytes(size_t count, size_t stride, size_t prefix, size_t *bytes)
{

	if (count > (SIZE_MAX - prefix) / stride)
		return EOVERFLOW;
	*bytes = prefix + count * stride;
	return 0;
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
    struct pvr_submit_batch *batch, uint32_t *index)
{
	struct drm_pvr_job input;
	struct pvr_submit_job *job;
	uint8_t stream[PVR_STREAM_COMPUTE_SIZE];
	uint64_t address;
	size_t bytes;
	uint32_t i, j;
	int error;

	for (i = 0; i < batch->count; i++) {
		*index = i;
		job = &batch->jobs[i];
		address = array->array + (uint64_t)i * sizeof(input);
		error = copyin((const void *)(uintptr_t)address, &input, sizeof(input));
		if (error != 0)
			return error;
		if (input.type != DRM_PVR_JOB_TYPE_COMPUTE)
			return EOPNOTSUPP;
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
		error = pvr_stream_compute(PVR_STREAM_BVNC, stream, sizeof(stream),
		    input.flags, &job->command);
		if (error != 0)
			return error;
		job->handle = input.context_handle;
		error = pvr_submit_array(&input.sync_ops, sizeof(struct drm_pvr_sync_op));
		if (error != 0)
			return error;
		if (input.sync_ops.count == 0)
			continue;
		error = pvr_submit_bytes(input.sync_ops.count, sizeof(*job->syncs),
		    0, &bytes);
		if (error != 0)
			return error;
		job->syncs = kmem_zalloc(bytes, KM_NOSLEEP);
		if (job->syncs == NULL)
			return ENOMEM;
		job->sync_count = input.sync_ops.count;
		for (j = 0; j < job->sync_count; j++) {
			address = input.sync_ops.array + (uint64_t)j * sizeof(struct drm_pvr_sync_op);
			error = copyin((const void *)(uintptr_t)address,
			    &job->syncs[j].input, sizeof(job->syncs[j].input));
			if (error != 0)
				return error;
			error = pvr_submit_sync_valid(&job->syncs[j].input);
			if (error != 0)
				return error;
		}
	}
	return 0;
}

/* The gate keeps context records live until their references are acquired. */
static int
pvr_submit_references(struct drm_device *dev, struct drm_file *file,
    struct pvr_submit_batch *batch, uint32_t *index)
{
	struct pvr_submit_job *job;
	struct pvr_submit_sync *sync;
	uint32_t i, j;
	int error;

	for (i = 0; i < batch->count; i++) {
		*index = i;
		job = &batch->jobs[i];
		job->context = pvr_exec_context_get(file, job->handle);
		if (job->context == NULL)
			return EINVAL;
		if (pvr_context_type(pvr_exec_context_firmware(job->context)) !=
		    DRM_PVR_CTX_TYPE_COMPUTE)
			return EINVAL;
		for (j = 0; j < job->sync_count; j++) {
			sync = &job->syncs[j];
			sync->object = drm_syncobj_find(file, sync->input.handle);
			if (sync->object == NULL)
				return EINVAL;
			if ((sync->input.flags & DRM_PVR_SYNC_OP_FLAG_SIGNAL) != 0 &&
			    sync->input.value != 0) {
				sync->chain = kzalloc(sizeof(*sync->chain), GFP_NOWAIT);
				if (sync->chain == NULL)
					return ENOMEM;
			}
		}
		error = pvr_device_next_id(dev, &job->id);
		if (error != 0)
			return error;
	}
	return 0;
}

/* A native chain's base does not inherit its contained fence's error. */
static int
pvr_submit_fence_status(struct dma_fence *fence)
{
	struct dma_fence_chain *chain;
	int status;

	for (;;) {
		status = dma_fence_get_status(fence);
		if (status <= 0)
			return status;
		chain = to_dma_fence_chain(fence);
		if (chain == NULL)
			return status;
		/* The held outer fence reference keeps this immutable child live. */
		fence = chain->dfc_fence;
	}
}

/* No device or platform gate may be held during this function. */
static int
pvr_submit_wait(struct pvr_submit_job *job)
{
	struct timespec now, deadline, remaining;
	struct dma_fence *fence;
	struct pvr_submit_sync *sync;
	uint64_t polls;
	uint32_t i;
	long waited;
	int error, status, ticks;

	getnanouptime(&deadline);
	deadline.tv_sec += PVR_SUBMIT_WAIT_SECONDS;
	polls = (uint64_t)MAX(hz, 1) * PVR_SUBMIT_WAIT_SECONDS + 1;
	for (i = 0; i < job->sync_count; i++) {
		sync = &job->syncs[i];
		if ((sync->input.flags & DRM_PVR_SYNC_OP_FLAG_SIGNAL) != 0)
			continue;
		for (;;) {
			getnanouptime(&now);
			if (timespeccmp(&now, &deadline, >=) || polls == 0)
				return ETIMEDOUT;
			/* A prior job may just have replaced this object's payload. */
			fence = drm_syncobj_fence_get(sync->object);
			if (fence != NULL) {
				error = dma_fence_chain_find_seqno(&fence, sync->input.value);
				if (error == 0) {
					/* A collected, completed point may have no remaining fence. */
					if (fence == NULL)
						break;
					timespecsub(&deadline, &now, &remaining);
					ticks = MAX(1, tstohz(&remaining));
					waited = dma_fence_wait_timeout(fence, true, ticks);
					status = waited > 0 ? pvr_submit_fence_status(fence) : 0;
					dma_fence_put(fence);
					/* A restart could repeat earlier completed jobs in this batch. */
					if (waited == -ERESTARTSYS)
						return EINTR;
					if (waited < 0)
						return (int)-waited;
					if (waited == 0)
						return ETIMEDOUT;
					if (status <= 0)
						return status < 0 ? -status : EIO;
					break;
				}
				dma_fence_put(fence);
				if (error != -EINVAL)
					return -error;
			}
			/* Wait for publication on the retained object, not its handle. */
			polls--;
			error = kpause("pvrdep", true, 1, NULL);
			if (error == ERESTART)
				return EINTR;
			if (error != 0 && error != EWOULDBLOCK)
				return error;
		}
	}
	return 0;
}

static void
pvr_submit_signal(struct pvr_submit_job *job)
{
	struct pvr_submit_sync *sync;
	struct dma_fence *completed;
	uint32_t i;

	/* Only call after real CCCB consumption, UFO completion, and health checks. */
	completed = dma_fence_get_stub();
	for (i = 0; i < job->sync_count; i++) {
		sync = &job->syncs[i];
		if ((sync->input.flags & DRM_PVR_SYNC_OP_FLAG_SIGNAL) == 0)
			continue;
		if (sync->chain != NULL) {
			drm_syncobj_add_point(sync->object, sync->chain,
			    completed, sync->input.value);
			sync->chain = NULL;
		} else {
			drm_syncobj_replace_fence(sync->object, completed);
		}
	}
	dma_fence_put(completed);
}

static void
pvr_submit_release(struct drm_device *dev, struct pvr_submit_batch *batch,
    bool gated)
{
	struct pvr_submit_job *job;
	uint32_t i, j;

	for (i = 0; i < batch->count; i++) {
		job = &batch->jobs[i];
		for (j = 0; j < job->sync_count; j++) {
			if (job->syncs[j].object != NULL)
				drm_syncobj_put(job->syncs[j].object);
			kfree(job->syncs[j].chain);
		}
		if (job->syncs != NULL)
			kmem_free(job->syncs, job->sync_count * sizeof(*job->syncs));
		if (job->context != NULL) {
			KASSERT(gated);
			pvr_exec_context_put(dev, job->context);
		}
	}
	kmem_free(batch, batch->bytes);
}

int
pvr_submit_jobs(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_pvr_ioctl_submit_jobs_args *args = data;
	struct drm_pvr_obj_array input = args->jobs;
	struct pvr_submit_batch *batch;
	struct pvr_submit_job *job;
	struct pvr_file_vm *vm;
	const struct pvr_backend *backend, *active_backend;
	size_t bytes;
	uint32_t index = 0;
	int error;

	args->jobs.count = 0;
	if (input.count == 0)
		return -EINVAL;
	error = pvr_submit_array(&input, sizeof(struct drm_pvr_job));
	if (error != 0)
		return -error;
	error = pvr_submit_bytes(input.count, sizeof(batch->jobs[0]),
	    sizeof(*batch), &bytes);
	if (error != 0)
		return -error;
	batch = kmem_zalloc(bytes, KM_NOSLEEP);
	if (batch == NULL)
		return -ENOMEM;
	batch->bytes = bytes;
	batch->count = input.count;
	error = pvr_submit_copy(&input, batch, &index);
	if (error != 0) {
		pvr_submit_release(dev, batch, false);
		args->jobs.count = index;
		return -error;
	}
	backend = pvr_device_enter(dev);
	if (backend == NULL) {
		error = EOPNOTSUPP;
		index = 0;
		goto out;
	}
	error = pvr_submit_references(dev, file, batch, &index);
	if (error != 0)
		goto out;
	for (index = 0; index < batch->count; index++) {
		job = &batch->jobs[index];
		pvr_device_leave(dev);
		error = pvr_submit_wait(job);
		active_backend = pvr_device_enter(dev);
		if (active_backend != backend) {
			/* Broken backend lifetime: preserve all references until reset. */
			pvr_device_retain(dev, batch);
			pvr_device_leave(dev);
			args->jobs.count = index;
			return -EIO;
		}
		if (error != 0)
			goto out;
		error = backend->check(backend->cookie);
		if (error != 0)
			goto out;
		vm = pvr_exec_context_vm(job->context);
		error = pvr_file_vm_prepare(vm);
		if (error != 0)
			goto out;
		error = pvr_context_compute(pvr_exec_context_firmware(job->context),
		    &job->command, job->id);
		if (error != 0) {
			/* The caller cannot prove that the GPU stopped using this VM. */
			pvr_file_vm_fail(vm);
			goto out;
		}
		pvr_file_vm_complete(vm);
		pvr_submit_signal(job);
	}
	error = 0;
out:
	pvr_submit_release(dev, batch, true);
	pvr_device_leave(dev);
	args->jobs.count = index;
	return -error;
}
