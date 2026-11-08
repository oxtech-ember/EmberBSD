/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Copyright (c) 2023 Imagination Technologies Ltd. */
/*
 * AI-assisted extraction of native batch policy from powervr/pvr_submit.c.
 * Origin: EmberBSD native submission, originally adapted from the MIT
 * alternative of Imagination Technologies' pvr_job.c and pvr_sync.c.
 * The existing DRM scheduler owns dispatch and deferred cleanup.
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
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <sys/timearith.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/dma-fence-chain.h>
#include <drm/drm_sched_batch.h>
#include <drm/drm_syncobj.h>

/* Common admission policy, independent of firmware and UAPI limits. */
#define DRM_BATCHES 8
#define DRM_BATCH_ITEMS 256
#define DRM_BATCH_BYTES (1U << 20)
#define DRM_BATCH_WAIT_SECONDS 10
#define DRM_BATCH_DEPENDENCY_CHECKS (1U << 20)

struct drm_sched_batch_queue {
	struct drm_gpu_scheduler scheduler;
	struct drm_sched_entity entity;
	kmutex_t lock;
	kcondvar_t drained;
	unsigned reserved, users;
	bool stopping;
};

struct drm_batch_fence {
	struct dma_fence base;
	spinlock_t lock;
};

static struct dma_fence *drm_batch_run(struct drm_sched_job *);
static void drm_batch_free(struct drm_sched_job *);

static struct dma_fence *
drm_batch_dependency(struct drm_sched_job *job, struct drm_sched_entity *entity)
{

	/* Strict completion/error waits are handled by the batch worker below. */
	return NULL;
}

static const struct drm_sched_backend_ops drm_batch_ops = {
	.dependency = drm_batch_dependency,
	.run_job = drm_batch_run,
	.free_job = drm_batch_free,
};

int
drm_sched_batch_queue_create(const char *name, struct drm_sched_batch_queue **out)
{
	struct drm_sched_batch_queue *queue;
	struct drm_gpu_scheduler *scheduler;
	int error;

	queue = kmem_zalloc(sizeof(*queue), KM_SLEEP);
	mutex_init(&queue->lock, MUTEX_DEFAULT, IPL_NONE);
	cv_init(&queue->drained, "drmbatch");
	scheduler = &queue->scheduler;
	/* The backend completes synchronously. Never replay uncertain DMA. */
	error = drm_sched_init(scheduler, &drm_batch_ops, 1, 0,
	    MAX_SCHEDULE_TIMEOUT, name);
	if (error != 0)
		goto fail;
	error = drm_sched_entity_init(&queue->entity, DRM_SCHED_PRIORITY_NORMAL,
	    &scheduler, 1, NULL);
	if (error != 0) {
		drm_sched_fini(scheduler);
		goto fail;
	}
	*out = queue;
	return 0;
fail:
	cv_destroy(&queue->drained);
	mutex_destroy(&queue->lock);
	kmem_free(queue, sizeof(*queue));
	return -error;
}

void
drm_sched_batch_queue_stop(struct drm_sched_batch_queue *queue)
{

	mutex_enter(&queue->lock);
	queue->stopping = true;
	mutex_exit(&queue->lock);
}

void
drm_sched_batch_queue_destroy(struct drm_sched_batch_queue *queue)
{

	if (queue == NULL)
		return;
	drm_sched_batch_queue_stop(queue);
	mutex_enter(&queue->lock);
	/* users includes reserve waiters, preparation and deferred release. */
	while (queue->users != 0)
		cv_wait(&queue->drained, &queue->lock);
	KASSERT(queue->reserved == 0);
	mutex_exit(&queue->lock);
	/* No queued jobs remain: entity_fini cannot use its fence-callback kill. */
	drm_sched_entity_fini(&queue->entity);
	drm_sched_fini(&queue->scheduler);
	cv_destroy(&queue->drained);
	mutex_destroy(&queue->lock);
	kmem_free(queue, sizeof(*queue));
}

bool
drm_sched_batch_stopping(struct drm_sched_batch_queue *queue)
{
	bool stopping;

	mutex_enter(&queue->lock);
	stopping = queue->stopping;
	mutex_exit(&queue->lock);
	return stopping;
}

int
drm_sched_batch_reserve(struct drm_sched_batch_queue *queue)
{
	int error;

	mutex_enter(&queue->lock);
	queue->users++;
	for (;;) {
		if (queue->stopping) {
			error = ENXIO;
			break;
		}
		if (queue->reserved < DRM_BATCHES) {
			queue->reserved++;
			mutex_exit(&queue->lock);
			return 0;
		}
		mutex_exit(&queue->lock);
		error = kpause("drmbatch", true, 1, NULL);
		mutex_enter(&queue->lock);
		if (error != 0 && error != EWOULDBLOCK) {
			if (error == ERESTART)
				error = EINTR;
			break;
		}
	}
	KASSERT(queue->users != 0);
	if (--queue->users == 0)
		cv_broadcast(&queue->drained);
	mutex_exit(&queue->lock);
	return error;
}

void
drm_sched_batch_unreserve(struct drm_sched_batch_queue *queue)
{

	mutex_enter(&queue->lock);
	KASSERT(queue->reserved != 0 && queue->users != 0);
	queue->reserved--;
	if (--queue->users == 0)
		cv_broadcast(&queue->drained);
	mutex_exit(&queue->lock);
}

static const char *
drm_batch_fence_name(struct dma_fence *fence)
{

	/* The name and release path must outlive the queue. */
	return "drm_batch";
}

static void
drm_batch_fence_free(struct rcu_head *rcu)
{
	struct dma_fence *base = container_of(rcu, struct dma_fence, rcu);
	struct drm_batch_fence *fence =
	    container_of(base, struct drm_batch_fence, base);

	dma_fence_destroy(base);
	spin_lock_destroy(&fence->lock);
	kfree(fence);
}

static void
drm_batch_fence_release(struct dma_fence *fence)
{

	call_rcu(&fence->rcu, drm_batch_fence_free);
}

static const struct dma_fence_ops drm_batch_fence_ops = {
	.use_64bit_seqno = true,
	.get_driver_name = drm_batch_fence_name,
	.get_timeline_name = drm_batch_fence_name,
	.release = drm_batch_fence_release,
};

static struct dma_fence *
drm_batch_fence_create(void)
{
	struct drm_batch_fence *fence;

	fence = kzalloc(sizeof(*fence), GFP_NOWAIT);
	if (fence == NULL)
		return NULL;
	spin_lock_init(&fence->lock);
	/* Preparation may finish out of order: no common output timeline. */
	dma_fence_init(&fence->base, &drm_batch_fence_ops, &fence->lock,
	    dma_fence_context_alloc(1), 1);
	return &fence->base;
}

int
drm_sched_batch_size(uint32_t count, size_t stride, size_t prefix, size_t *bytes)
{

	if (count == 0 || stride == 0)
		return EINVAL;
	if (count > DRM_BATCH_ITEMS)
		return E2BIG;
	if (count > (SIZE_MAX - prefix) / stride)
		return EOVERFLOW;
	*bytes = prefix + count * stride;
	return *bytes > DRM_BATCH_BYTES ? E2BIG : 0;
}

struct drm_sched_batch_item *
drm_sched_batch_item(struct drm_sched_batch *batch, uint32_t index)
{

	KASSERT(index < batch->count);
	return (void *)((uint8_t *)batch->items + index * batch->stride);
}

int
drm_sched_batch_init(struct drm_sched_batch *batch,
    struct drm_sched_batch_queue *queue, struct drm_sched_batch_item *items,
    size_t stride, uint32_t count, size_t bytes,
    const struct drm_sched_batch_ops *ops, void *data)
{
	uint32_t i;
	size_t size;
	int error;

	error = drm_sched_batch_size(count, stride, 0, &size);
	if (error != 0)
		return error;
	if (stride < sizeof(*items) || size > bytes || items == NULL ||
	    ops == NULL || ops->execute == NULL || ops->release == NULL)
		return EINVAL;
	if (bytes > DRM_BATCH_BYTES - sizeof(struct drm_sched_fence) ||
	    count > (DRM_BATCH_BYTES - sizeof(struct drm_sched_fence) - bytes) /
	    sizeof(struct drm_batch_fence))
		return E2BIG;
	batch->queue = queue;
	batch->items = items;
	batch->stride = stride;
	batch->count = count;
	batch->ops = ops;
	batch->data = data;
	batch->charged = bytes + count * sizeof(struct drm_batch_fence) +
	    sizeof(struct drm_sched_fence);
	for (i = 0; i < count; i++)
		drm_sched_batch_item(batch, i)->first = i;
	return 0;
}

int
drm_sched_batch_sync_alloc(struct drm_sched_batch *batch, uint32_t index,
    uint32_t count)
{
	struct drm_sched_batch_item *item = drm_sched_batch_item(batch, index);
	size_t stride = sizeof(*item->syncs) + sizeof(struct dma_fence_chain);

	KASSERT(item->syncs == NULL && item->sync_count == 0);
	if (count == 0)
		return 0;
	if (count > (DRM_BATCH_BYTES - batch->charged) / stride)
		return E2BIG;
	item->syncs = kmem_zalloc(count * sizeof(*item->syncs), KM_NOSLEEP);
	if (item->syncs == NULL)
		return ENOMEM;
	item->sync_count = count;
	batch->charged += count * stride;
	return 0;
}

int
drm_sched_batch_prepare(struct drm_sched_batch *batch, struct drm_file *file)
{
	struct drm_sched_batch_item *item;
	struct drm_sched_batch_sync *sync;
	uint32_t i, j;

	for (i = 0; i < batch->count; i++) {
		item = drm_sched_batch_item(batch, i);
		if (item->first > i ||
		    drm_sched_batch_item(batch, item->first)->first != item->first ||
		    (item->first != i &&
		    drm_sched_batch_item(batch, i - 1)->first != item->first))
			return EINVAL;
		for (j = 0; j < item->sync_count; j++) {
			sync = &item->syncs[j];
			if (!sync->timeline && sync->value != 0)
				return EINVAL;
			sync->object = drm_syncobj_find(file, sync->handle);
			if (sync->object == NULL)
				return EINVAL;
			if (sync->signal && sync->value != 0) {
				sync->chain = kzalloc(sizeof(*sync->chain), GFP_NOWAIT);
				if (sync->chain == NULL)
					return ENOMEM;
			}
		}
		item->finished = drm_batch_fence_create();
		if (item->finished == NULL)
			return ENOMEM;
	}
	return 0;
}

static int
drm_batch_producer(struct drm_sched_batch *batch, uint32_t first, uint32_t end,
    const struct drm_sched_batch_sync *wait, bool exact, uint32_t *budget,
    bool *found, struct dma_fence **producer)
{
	struct drm_sched_batch_item *item;
	const struct drm_sched_batch_sync *signal;
	uint32_t i, j;

	KASSERT(first <= end && end <= batch->count);
	*found = false;
	for (i = first; i < end; i++) {
		if (*budget == 0)
			return E2BIG;
		(*budget)--;
		item = drm_sched_batch_item(batch, i);
		for (j = 0; j < item->sync_count; j++) {
			if (*budget == 0)
				return E2BIG;
			(*budget)--;
			signal = &item->syncs[j];
			if (!signal->signal || signal->object != wait->object ||
			    signal->timeline != wait->timeline)
				continue;
			if (exact ? signal->value != wait->value : signal->value < wait->value)
				continue;
			*found = true;
			if (producer != NULL)
				*producer = dma_fence_get(item->finished);
			return 0;
		}
	}
	return 0;
}

static int
drm_batch_capture_item(struct drm_sched_batch *batch, uint32_t index,
    uint32_t *budget)
{
	struct drm_sched_batch_item *item = drm_sched_batch_item(batch, index);
	struct drm_sched_batch_sync *sync;
	struct dma_fence *fence;
	struct timespec now, deadline;
	uint32_t i, prior;
	uint64_t polls;
	bool checked, matched;
	int error;

	drm_sched_batch_deadline(&deadline);
	polls = (uint64_t)MAX(hz, 1) * DRM_BATCH_WAIT_SECONDS + 1;
	for (i = 0; i < item->sync_count; i++) {
		sync = &item->syncs[i];
		if (sync->signal)
			continue;
		matched = false;
		for (prior = item->first; prior < index && !matched; prior++) {
			if (batch->ops->internal_dependency == NULL ||
			    !batch->ops->internal_dependency(batch->data, index, prior))
				continue;
			error = drm_batch_producer(batch, prior, prior + 1, sync, true,
			    budget, &matched, NULL);
			if (error != 0)
				return error;
		}
		if (matched)
			continue;
		/* Latest earlier signal supersedes the previous binary payload. */
		for (prior = item->first; prior != 0 && !matched; prior--) {
			error = drm_batch_producer(batch, prior - 1, prior, sync, false,
			    budget, &matched, &sync->wait);
			if (error != 0)
				return error;
		}
		if (matched)
			continue;
		checked = false;
		for (;;) {
			if (drm_sched_batch_stopping(batch->queue))
				return ENXIO;
			fence = drm_syncobj_fence_get(sync->object);
			if (fence != NULL) {
				error = dma_fence_chain_find_seqno(&fence, sync->value);
				if (error == 0) {
					sync->wait = fence;
					break;
				}
				dma_fence_put(fence);
				if (error != -EINVAL)
					return -error;
			}
			if (!checked) {
				error = drm_batch_producer(batch, item->first, batch->count,
				    sync, false, budget, &matched, NULL);
				if (error != 0)
					return error;
				if (matched)
					return EDEADLK;
				checked = true;
			}
			getnanouptime(&now);
			if (timespeccmp(&now, &deadline, >=) || polls-- == 0)
				return ETIMEDOUT;
			error = kpause("drmbdep", true, 1, NULL);
			if (error != 0 && error != EWOULDBLOCK)
				return error == ERESTART ? EINTR : error;
		}
	}
	return 0;
}

int
drm_sched_batch_capture(struct drm_sched_batch *batch)
{
	uint32_t i, budget = DRM_BATCH_DEPENDENCY_CHECKS;
	int error;

	/* Capture all inputs before any output can replace a binary payload. */
	for (i = 0; i < batch->count; i++) {
		error = drm_batch_capture_item(batch, i, &budget);
		if (error != 0)
			return error;
	}
	return 0;
}

int
drm_sched_batch_commit(struct drm_sched_batch *batch)
{
	struct drm_sched_batch_queue *queue = batch->queue;
	struct drm_sched_batch_item *item;
	struct drm_sched_batch_sync *sync;
	uint32_t i, j;
	int error;

	mutex_enter(&queue->lock);
	if (queue->stopping) {
		mutex_exit(&queue->lock);
		return ENXIO;
	}
	error = drm_sched_job_init(&batch->job, &queue->entity, batch->data);
	if (error != 0) {
		mutex_exit(&queue->lock);
		return -error;
	}
	/* No fallible operation remains. Serialize init, publication and push. */
	for (i = 0; i < batch->count; i++) {
		item = drm_sched_batch_item(batch, i);
		for (j = 0; j < item->sync_count; j++) {
			sync = &item->syncs[j];
			if (!sync->signal)
				continue;
			if (sync->chain != NULL) {
				drm_syncobj_add_point(sync->object, sync->chain,
				    item->finished, sync->value);
				sync->chain = NULL;
			} else {
				drm_syncobj_replace_fence(sync->object, item->finished);
			}
		}
	}
	drm_sched_entity_push_job(&batch->job, &queue->entity);
	mutex_exit(&queue->lock);
	return 0;
}

static bool
drm_batch_cancelled(struct drm_sched_batch *batch, uint32_t index)
{

	return drm_sched_batch_stopping(batch->queue) ||
	    (batch->ops->cancelled != NULL &&
	    batch->ops->cancelled(batch->data, index));
}

static int
drm_batch_wait(struct drm_sched_batch *batch, uint32_t index)
{
	struct drm_sched_batch_item *item = drm_sched_batch_item(batch, index);
	struct timespec now, deadline;
	struct dma_fence *fence;
	uint32_t i;
	long waited;
	int status;

	drm_sched_batch_deadline(&deadline);
	for (i = 0; i < item->sync_count; i++) {
		fence = item->syncs[i].wait;
		if (fence == NULL)
			continue;
		for (;;) {
			if (drm_batch_cancelled(batch, index))
				return ECANCELED;
			status = dma_fence_chain_get_payload_status(fence);
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
	return drm_batch_cancelled(batch, index) ? ECANCELED : 0;
}

void
drm_sched_batch_deadline(struct timespec *deadline)
{

	getnanouptime(deadline);
	deadline->tv_sec += DRM_BATCH_WAIT_SECONDS;
}

static void
drm_batch_signal(struct drm_sched_batch *batch, uint32_t index, int error)
{
	struct dma_fence *fence = drm_sched_batch_item(batch, index)->finished;

	if (error != 0)
		dma_fence_set_error(fence, -error);
	dma_fence_signal(fence);
}

static struct dma_fence *
drm_batch_run(struct drm_sched_job *job)
{
	struct drm_sched_batch *batch = container_of(job, struct drm_sched_batch, job);
	uint32_t first = 0, end, i;
	int error = 0;

	while (first < batch->count) {
		for (end = first + 1; end < batch->count; end++)
			if (drm_sched_batch_item(batch, end)->first != first)
				break;
		for (i = first; i < end; i++) {
			error = drm_batch_wait(batch, i);
			if (error != 0)
				break;
		}
		if (error == 0)
			error = batch->ops->execute(batch->data, first);
		if (error != 0)
			break;
		for (i = first; i < end; i++)
			drm_batch_signal(batch, i, 0);
		first = end;
	}
	for (i = first; i < batch->count; i++)
		drm_batch_signal(batch, i, error);
	return error == 0 ? NULL : ERR_PTR(-error);
}

void
drm_sched_batch_cleanup(struct drm_sched_batch *batch)
{
	struct drm_sched_batch_item *item;
	uint32_t i, j;

	for (i = 0; i < batch->count; i++) {
		item = drm_sched_batch_item(batch, i);
		for (j = 0; j < item->sync_count; j++) {
			if (item->syncs[j].object != NULL)
				drm_syncobj_put(item->syncs[j].object);
			kfree(item->syncs[j].chain);
			dma_fence_put(item->syncs[j].wait);
		}
		if (item->syncs != NULL)
			kmem_free(item->syncs, item->sync_count * sizeof(*item->syncs));
		dma_fence_put(item->finished);
	}
}

static void
drm_batch_free(struct drm_sched_job *job)
{
	struct drm_sched_batch *batch = container_of(job, struct drm_sched_batch, job);
	struct drm_sched_batch_queue *queue = batch->queue;

	drm_sched_job_cleanup(job);
	batch->ops->release(batch->data);
	/* Final queue access; destroy joins the scheduler thread before freeing. */
	drm_sched_batch_unreserve(queue);
}
