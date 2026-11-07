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
#include <sys/workqueue.h>
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

/* Bounded native scheduler policy, not firmware or UAPI size limits. */
#define PVR_SUBMIT_BATCHES 8
#define PVR_SUBMIT_JOBS 256
#define PVR_SUBMIT_BYTES (1U << 20)
#define PVR_SUBMIT_WAIT_SECONDS 10
/* Bound all producer searches in one batch, including empty jobs. */
#define PVR_SUBMIT_DEPENDENCY_CHECKS (1U << 20)

/* GE8300 streams are copied into the buffers sized for the other profile. */
CTASSERT(PVR_STREAM_GE8300_TRANSFER_SIZE <= PVR_STREAM_TRANSFER_SIZE);
CTASSERT(PVR_STREAM_GE8300_GEOMETRY_SIZE <= PVR_STREAM_FRAGMENT_SIZE);

struct pvr_submit_sync {
	struct drm_pvr_sync_op input;
	struct drm_syncobj *object;
	struct dma_fence_chain *chain;
	struct dma_fence *wait;
};
struct pvr_submit_job {
	union {
		struct rogue_fwif_cmd_compute compute;
		struct rogue_fwif_cmd_transfer transfer;
		struct rogue_fwif_cmd_geom geometry;
		struct rogue_fwif_cmd_frag fragment;
	} command;
	struct pvr_exec_context *context;
	struct pvr_exec_hwrt *hwrt;
	struct pvr_submit_sync *syncs;
	uint32_t handle, sync_count, id;
	uint32_t hwrt_handle;
	uint32_t type;
	uint8_t rt_index;
	uint32_t fragment_type;
	bool paired_fragment, vm_held;
	/* Held VM, recorded under the gate; the worker reads it while waiting. */
	struct pvr_file_vm *vm;
	struct dma_fence *finished;
};
struct pvr_submit_batch {
	struct work work;
	struct pvr_submit_queue *queue;
	const struct pvr_backend *backend;
	size_t bytes, charged;
	uint32_t count;
	struct pvr_submit_job jobs[];
};

struct pvr_submit_queue {
	struct drm_device *dev;
	struct workqueue *worker;
	struct work drain;
	kmutex_t lock;
	unsigned reserved;
	bool stopping;
};
struct pvr_submit_fence {
	struct dma_fence base;
	spinlock_t lock;
};

static void pvr_submit_execute(struct work *, void *);

static const char *
pvr_submit_fence_name(struct dma_fence *fence)
{
	return "powervr";
}

static void
pvr_submit_fence_free(struct rcu_head *rcu)
{
	struct dma_fence *base = container_of(rcu, struct dma_fence, rcu);
	struct pvr_submit_fence *fence = container_of(base, struct pvr_submit_fence, base);

	dma_fence_destroy(base);
	spin_lock_destroy(&fence->lock);
	kfree(fence);
}

static void
pvr_submit_fence_release(struct dma_fence *fence)
{
	call_rcu(&fence->rcu, pvr_submit_fence_free);
}

static const struct dma_fence_ops pvr_submit_fence_ops = {
	.use_64bit_seqno = true,
	.get_driver_name = pvr_submit_fence_name,
	.get_timeline_name = pvr_submit_fence_name,
	.release = pvr_submit_fence_release,
};

static struct dma_fence *
pvr_submit_fence_create(void)
{
	struct pvr_submit_fence *fence;

	fence = kzalloc(sizeof(*fence), GFP_NOWAIT);
	if (fence == NULL)
		return NULL;
	spin_lock_init(&fence->lock);
	/* Preparation may finish out of order: do not claim a common timeline. */
	dma_fence_init(&fence->base, &pvr_submit_fence_ops, &fence->lock,
	    dma_fence_context_alloc(1), 1);
	return &fence->base;
}

int
pvr_submit_queue_create(struct drm_device *dev, struct pvr_submit_queue **out)
{
	struct pvr_submit_queue *queue;
	int error;

	queue = kmem_zalloc(sizeof(*queue), KM_SLEEP);
	queue->dev = dev;
	mutex_init(&queue->lock, MUTEX_DEFAULT, IPL_NONE);
	error = workqueue_create(&queue->worker, "pvrsubmit", pvr_submit_execute,
	    queue, PRI_NONE, IPL_NONE, WQ_MPSAFE);
	if (error != 0) {
		mutex_destroy(&queue->lock);
		kmem_free(queue, sizeof(*queue));
		return error;
	}
	*out = queue;
	return 0;
}

/* Attachment owns dev until reboot. Any future detach must stop admission and
 * call this after excluding ioctls and before dropping that owner, without
 * either execution gate held. */
void
pvr_submit_queue_destroy(struct pvr_submit_queue *queue)
{

	if (queue == NULL)
		return;
	mutex_enter(&queue->lock);
	queue->stopping = true;
	mutex_exit(&queue->lock);
	/* NetBSD workqueue_destroy replaces the callback; it does not drain jobs.
	 * A FIFO marker waits for queued cancellations and active DMA retirement. */
	workqueue_enqueue(queue->worker, &queue->drain, NULL);
	workqueue_wait(queue->worker, &queue->drain);
	workqueue_destroy(queue->worker);
	KASSERT(queue->reserved == 0);
	mutex_destroy(&queue->lock);
	kmem_free(queue, sizeof(*queue));
}

static bool
pvr_submit_stopping(struct pvr_submit_queue *queue)
{
	bool stopping;

	mutex_enter(&queue->lock);
	stopping = queue->stopping;
	mutex_exit(&queue->lock);
	return stopping;
}

static int
pvr_submit_reserve(struct pvr_submit_queue *queue)
{
	int error;

	for (;;) {
		mutex_enter(&queue->lock);
		if (queue->stopping) {
			mutex_exit(&queue->lock);
			return ENXIO;
		}
		if (queue->reserved < PVR_SUBMIT_BATCHES) {
			queue->reserved++;
			mutex_exit(&queue->lock);
			return 0;
		}
		mutex_exit(&queue->lock);
		error = kpause("pvrqueue", true, 1, NULL);
		if (error != 0 && error != EWOULDBLOCK)
			return error == ERESTART ? EINTR : error;
	}
}

static void
pvr_submit_unreserve(struct pvr_submit_queue *queue)
{

	mutex_enter(&queue->lock);
	KASSERT(queue->reserved != 0);
	queue->reserved--;
	mutex_exit(&queue->lock);
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
    struct pvr_submit_batch *batch, uint32_t *index, uint64_t bvnc)
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
		error = pvr_submit_bytes(input.sync_ops.count, sizeof(*job->syncs),
		    0, &bytes);
		if (error != 0)
			return error;
		if (bytes > PVR_SUBMIT_BYTES - batch->charged ||
		    input.sync_ops.count > (PVR_SUBMIT_BYTES - batch->charged - bytes) /
		    sizeof(struct dma_fence_chain))
			return E2BIG;
		batch->charged += bytes + input.sync_ops.count * sizeof(struct dma_fence_chain);
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
	struct pvr_submit_sync *sync;
	uint32_t i, j;
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
		job->finished = pvr_submit_fence_create();
		if (job->finished == NULL)
			return ENOMEM;
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

/* References and copied sync arrays stay live while both gates are released. */
static int
pvr_submit_producer(const struct pvr_submit_batch *batch, uint32_t first,
    uint32_t end, const struct pvr_submit_sync *wait, bool exact,
    uint32_t *budget, bool *found, struct dma_fence **producer)
{
	const struct pvr_submit_job *job;
	uint32_t i, j, type;

	KASSERT(first <= end && end <= batch->count);
	*found = false;
	type = wait->input.flags & DRM_PVR_SYNC_OP_FLAG_HANDLE_TYPE_MASK;
	for (i = first; i < end; i++) {
		if (*budget == 0)
			return E2BIG;
		(*budget)--;
		job = &batch->jobs[i];
		for (j = 0; j < job->sync_count; j++) {
			const struct pvr_submit_sync *signal;

			if (*budget == 0)
				return E2BIG;
			(*budget)--;
			signal = &job->syncs[j];
			if ((signal->input.flags & DRM_PVR_SYNC_OP_FLAG_SIGNAL) == 0 ||
			    signal->object != wait->object ||
			    (signal->input.flags &
			    DRM_PVR_SYNC_OP_FLAG_HANDLE_TYPE_MASK) != type)
				continue;
			if (exact ? signal->input.value != wait->input.value :
			    signal->input.value < wait->input.value)
				continue;
			*found = true;
			if (producer != NULL)
				*producer = dma_fence_get(job->finished);
			return 0;
		}
	}
	return 0;
}

/*
 * An unpublished self or forward dependency cannot run in this serial
 * batch. A published fence can have an external producer and must be waited.
 */
static int
pvr_submit_forward(const struct pvr_submit_batch *batch, uint32_t first,
    const struct pvr_submit_sync *sync, uint32_t *budget, bool *checked)
{
	bool found;
	int error;

	if (*checked)
		return 0;
	error = pvr_submit_producer(batch, first, batch->count, sync, false,
	    budget, &found, NULL);
	if (error != 0)
		return error;
	*checked = true;
	return found ? EDEADLK : 0;
}

/* Capture every input before publishing any output. Only publication can wait
 * here; GPU completion is the worker's responsibility. Never reacquire a binary
 * syncobj's current payload later: it may already be this batch's own output. */
static int
pvr_submit_capture(struct pvr_submit_batch *batch, uint32_t index,
    uint32_t *budget)
{
	struct pvr_submit_job *job = &batch->jobs[index];
	struct pvr_submit_sync *sync;
	struct dma_fence *fence;
	struct timespec now, deadline;
	uint32_t first = job->paired_fragment ? index - 1 : index;
	uint32_t i, prior;
	uint64_t polls;
	bool checked, matched;
	int error;

	getnanouptime(&deadline);
	deadline.tv_sec += PVR_SUBMIT_WAIT_SECONDS;
	polls = (uint64_t)MAX(hz, 1) * PVR_SUBMIT_WAIT_SECONDS + 1;
	for (i = 0; i < job->sync_count; i++) {
		sync = &job->syncs[i];
		if ((sync->input.flags & DRM_PVR_SYNC_OP_FLAG_SIGNAL) != 0)
			continue;
		if (job->paired_fragment) {
			error = pvr_submit_producer(batch, first, index, sync, true,
			    budget, &matched, NULL);
			if (error != 0)
				return error;
			if (matched)
				continue; /* Firmware FENCE_PR, not a host self-dependency. */
		}
		/* The latest earlier matching signal replaces an older binary payload. */
		matched = false;
		for (prior = first; prior != 0 && !matched; prior--) {
			error = pvr_submit_producer(batch, prior - 1, prior, sync, false,
			    budget, &matched, &sync->wait);
			if (error != 0)
				return error;
		}
		if (matched)
			continue;
		checked = false;
		for (;;) {
			if (pvr_submit_stopping(batch->queue))
				return ENXIO;
			fence = drm_syncobj_fence_get(sync->object);
			if (fence != NULL) {
				error = dma_fence_chain_find_seqno(&fence, sync->input.value);
				if (error == 0) {
					sync->wait = fence;
					break; /* NULL means an already collected completed point. */
				}
				dma_fence_put(fence);
				if (error != -EINVAL)
					return -error;
			}
			error = pvr_submit_forward(batch, first, sync, budget, &checked);
			if (error != 0)
				return error;
			getnanouptime(&now);
			if (timespeccmp(&now, &deadline, >=) || polls-- == 0)
				return ETIMEDOUT;
			error = kpause("pvrdep", true, 1, NULL);
			if (error != 0 && error != EWOULDBLOCK)
				return error == ERESTART ? EINTR : error;
		}
	}
	return 0;
}

/* No gates held. Short waits allow shutdown to cancel external dependencies. */
static int
pvr_submit_wait(struct pvr_submit_batch *batch, struct pvr_submit_job *job)
{
	struct timespec now, deadline;
	struct dma_fence *fence;
	uint32_t i;
	long waited;
	int status;

	getnanouptime(&deadline);
	deadline.tv_sec += PVR_SUBMIT_WAIT_SECONDS;
	for (i = 0; i < job->sync_count; i++) {
		fence = job->syncs[i].wait;
		if (fence == NULL)
			continue;
		for (;;) {
			if (pvr_submit_stopping(batch->queue) ||
			    (job->vm != NULL && pvr_file_vm_closed(job->vm)))
				return ECANCELED;
			status = pvr_submit_fence_status(fence);
			if (status != 0) {
				if (status < 0)
					return -status;
				break;
			}
			getnanouptime(&now);
			if (timespeccmp(&now, &deadline, >=))
				return ETIMEDOUT;
			waited = dma_fence_wait_timeout(fence, false, MAX(1, hz / 100));
			if (waited < 0)
				return waited == -ERESTARTSYS ? EINTR : (int)-waited;
		}
	}
	return 0;
}

static void
pvr_submit_publish(struct pvr_submit_job *job)
{
	struct pvr_submit_sync *sync;
	uint32_t i;

	for (i = 0; i < job->sync_count; i++) {
		sync = &job->syncs[i];
		if ((sync->input.flags & DRM_PVR_SYNC_OP_FLAG_SIGNAL) == 0)
			continue;
		if (sync->chain != NULL) {
			drm_syncobj_add_point(sync->object, sync->chain,
			    job->finished, sync->input.value);
			sync->chain = NULL;
		} else {
			drm_syncobj_replace_fence(sync->object, job->finished);
		}
	}
}

static void
pvr_submit_signal(struct pvr_submit_job *job, int error)
{

	if (error != 0)
		dma_fence_set_error(job->finished, -error);
	dma_fence_signal(job->finished);
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
			dma_fence_put(job->syncs[j].wait);
		}
		if (job->syncs != NULL)
			kmem_free(job->syncs, job->sync_count * sizeof(*job->syncs));
		dma_fence_put(job->finished);
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

static void
pvr_submit_execute(struct work *work, void *cookie)
{
	struct pvr_submit_queue *queue = cookie;
	struct pvr_submit_batch *batch;
	struct drm_device *dev = queue->dev;
	const struct pvr_backend *backend;
	struct pvr_submit_job *job;
	struct pvr_file_vm *vm;
	uint32_t index;
	int error = 0;

	if (work == &queue->drain)
		return;
	batch = container_of(work, struct pvr_submit_batch, work);
	backend = batch->backend;
	for (index = 0; index < batch->count; index++) {
		job = &batch->jobs[index];
		if (job->paired_fragment)
			continue;
		error = pvr_submit_wait(batch, job);
		if (error == 0 && job->type == DRM_PVR_JOB_TYPE_GEOMETRY)
			error = pvr_submit_wait(batch, &batch->jobs[index + 1]);
		(void)pvr_device_enter(dev);
		vm = pvr_exec_context_vm(job->context);
		/* Work of a closed file has no consumer; do not run it. */
		if (error == 0 && (pvr_submit_stopping(queue) ||
		    pvr_file_vm_closed(vm)))
			error = ECANCELED;
		if (error == 0)
			error = backend->check(backend->cookie);
		if (error == 0)
			error = pvr_file_vm_prepare(vm);
		if (error != 0) {
			pvr_device_leave(dev);
			break;
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
		if (error != 0)
			break;
		/* Signal outside execution gates: fence callbacks may take other locks. */
		pvr_submit_signal(job, 0);
		if (job->type == DRM_PVR_JOB_TYPE_GEOMETRY)
			pvr_submit_signal(&batch->jobs[index + 1], 0);
	}
	if (error != 0)
		for (; index < batch->count; index++)
			pvr_submit_signal(&batch->jobs[index], error);
	(void)pvr_device_enter(dev);
	pvr_submit_release(dev, batch, true);
	pvr_device_leave(dev);
	pvr_submit_unreserve(queue);
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
	uint32_t index = 0, budget = PVR_SUBMIT_DEPENDENCY_CHECKS;
	int error, paused;

	args->jobs.count = 0;
	if (input.count == 0)
		return -EINVAL;
	if (input.count > PVR_SUBMIT_JOBS)
		return -E2BIG;
	error = pvr_submit_array(&input, sizeof(struct drm_pvr_job));
	if (error != 0)
		return -error;
	error = pvr_submit_bytes(input.count, sizeof(batch->jobs[0]), sizeof(*batch), &bytes);
	if (error != 0)
		return -error;
	error = pvr_submit_reserve(queue);
	if (error != 0)
		return -error;
	batch = kmem_zalloc(bytes, KM_NOSLEEP);
	if (batch == NULL) {
		pvr_submit_unreserve(queue);
		return -ENOMEM;
	}
	batch->queue = queue;
	batch->bytes = bytes;
	batch->charged = bytes + input.count * sizeof(struct pvr_submit_fence);
	batch->count = input.count;
	error = pvr_submit_copy(&input, batch, &index, pvr_device_bvnc(dev));
	if (error != 0) {
		pvr_submit_release(dev, batch, false);
		pvr_submit_unreserve(queue);
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
	for (index = 0; index < batch->count; index++) {
		error = pvr_submit_capture(batch, index, &budget);
		if (error != 0)
			break;
	}
	if (pvr_device_enter(dev) != backend) {
		/* A backend owner must never change while retained contexts exist. */
		pvr_device_retain(dev, batch);
		pvr_device_leave(dev);
		pvr_submit_unreserve(queue);
		return -EIO;
	}
	if (error != 0)
		goto out;
	if (pvr_submit_stopping(queue)) {
		error = ENXIO;
		goto out;
	}
	getnanouptime(&deadline);
	deadline.tv_sec += PVR_SUBMIT_WAIT_SECONDS;
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
			pvr_submit_unreserve(queue);
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
		if (pvr_submit_stopping(queue)) {
			error = ENXIO;
			goto out;
		}
	}
	if (error != 0)
		goto out;
	/* Publication and enqueue have one order under the device gate. Another
	 * submit may capture these fences but cannot enqueue ahead of this batch. */
	for (index = 0; index < batch->count; index++)
		pvr_submit_publish(&batch->jobs[index]);
	workqueue_enqueue(queue->worker, &batch->work, NULL);
	pvr_device_leave(dev);
	args->jobs.count = input.count;
	return 0;
out:
	pvr_submit_release(dev, batch, true);
	pvr_device_leave(dev);
	pvr_submit_unreserve(queue);
	return -error;
}
