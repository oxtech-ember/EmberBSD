/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* AI-assisted BO reader fencing. The caller owns actual hardware retirement. */
#include <sys/param.h>
#include <sys/errno.h>
#include <linux/dma-fence.h>
#include <linux/dma-resv.h>
#include <linux/err.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>
#include <drm/drm_gem.h>
#include "bcmv3d_read.h"

struct bcmv3d_read_fence {
	struct dma_fence base;
	spinlock_t lock;
};

static const char *
bcmv3d_read_name(struct dma_fence *fence)
{

	return "bcmv3d-read";
}

static void
bcmv3d_read_free_rcu(struct rcu_head *head)
{
	struct dma_fence *fence = container_of(head, struct dma_fence, rcu);
	struct bcmv3d_read_fence *read = container_of(fence,
	    struct bcmv3d_read_fence, base);

	dma_fence_destroy(fence);
	spin_lock_destroy(&read->lock);
	kfree(read);
}

static void
bcmv3d_read_release(struct dma_fence *fence)
{

	/* RCU readers can still inspect the embedded lock after the last put. */
	call_rcu(&fence->rcu, bcmv3d_read_free_rcu);
}

static const struct dma_fence_ops bcmv3d_read_ops = {
	.use_64bit_seqno = true,
	.get_driver_name = bcmv3d_read_name,
	.get_timeline_name = bcmv3d_read_name,
	.release = bcmv3d_read_release,
};

int
bcmv3d_read_reserve(struct bcmv3d_read *s, struct drm_gem_object *obj)
{
	struct bcmv3d_read_fence *read;
	struct dma_fence *prior;
	int ret, error;

	if (s == NULL || obj == NULL || obj->resv == NULL)
		return EINVAL;
	if (s->owner != NULL || s->object != NULL || s->fence != NULL ||
	    s->prior != NULL || s->poisoned)
		return EBUSY;
	read = kzalloc(sizeof(*read), GFP_KERNEL);
	if (read == NULL)
		return ENOMEM;
	spin_lock_init(&read->lock);
	dma_fence_init(&read->base, &bcmv3d_read_ops, &read->lock,
	    dma_fence_context_alloc(1), 1);
	drm_gem_object_get(obj);
	ret = dma_resv_lock_interruptible(obj->resv, NULL);
	if (ret != 0) {
		error = ret < 0 ? -ret : EIO;
		goto fail;
	}
	prior = dma_fence_get(dma_resv_get_excl(obj->resv));
	ret = dma_resv_reserve_shared(obj->resv, 1);
	error = ret < 0 ? -ret : ret == 0 ? 0 : EIO;
	if (error != 0) {
		dma_fence_put(prior);
		dma_resv_unlock(obj->resv);
		goto fail;
	}
	dma_resv_add_shared_fence(obj->resv, &read->base);
	dma_resv_unlock(obj->resv);
	s->owner = s;
	s->object = obj;
	s->fence = &read->base;
	s->prior = prior;
	return 0;
fail:
	drm_gem_object_put_unlocked(obj);
	dma_fence_put(&read->base);
	return error;
}

int
bcmv3d_read_wait(struct bcmv3d_read *s, long timeout)
{
	struct dma_fence *prior;
	long waited;
	int ret, error;

	if (s == NULL || timeout <= 0)
		return EINVAL;
	if (s->owner != s)
		return EPERM;
	if (s->object == NULL || s->fence == NULL)
		return EINVAL;
	if (s->poisoned)
		return EBUSY;
	prior = s->prior;
	if (prior == NULL)
		return 0;
	waited = dma_fence_wait_timeout(prior, true, timeout);
	if (waited <= 0)
		error = waited == 0 ? ETIME : -waited;
	else {
		ret = dma_fence_get_status(prior);
		error = ret > 0 ? 0 : ret == 0 ? EIO : -ret;
	}
	/* A failed wait keeps the original dependency for a retry or cancellation. */
	if (error == 0) {
		s->prior = NULL;
		dma_fence_put(prior);
	}
	return error;
}

int
bcmv3d_read_begin(struct bcmv3d_read *s, struct drm_gem_object *obj,
    long timeout)
{
	int error, cleanup;

	if (timeout <= 0)
		return EINVAL;
	error = bcmv3d_read_reserve(s, obj);
	if (error != 0)
		return error;
	error = bcmv3d_read_wait(s, timeout);
	if (error != 0) {
		cleanup = bcmv3d_read_end(s);
		if (cleanup != 0)
			return cleanup;
	}
	return error;
}

int
bcmv3d_read_end(struct bcmv3d_read *s)
{
	struct drm_gem_object *obj;
	struct dma_fence *fence;
	int ret;

	if (s == NULL)
		return EINVAL;
	if (s->owner == NULL && s->object == NULL && s->fence == NULL &&
	    s->prior == NULL && !s->poisoned)
		return 0;
	if (s->owner != s)
		return EPERM;
	if (s->object == NULL || s->fence == NULL)
		return EINVAL;
	if (s->poisoned)
		return EBUSY;
	if (dma_fence_get_status(s->fence) != 0) {
		s->poisoned = true;
		return EALREADY;
	}
	ret = dma_fence_signal(s->fence);
	if (ret != 0) {
		s->poisoned = true;
		return ret < 0 ? -ret : EIO;
	}
	obj = s->object;
	fence = s->fence;
	s->owner = NULL;
	s->object = NULL;
	s->fence = NULL;
	dma_fence_put(s->prior);
	s->prior = NULL;
	drm_gem_object_put_unlocked(obj);
	dma_fence_put(fence);
	return 0;
}

int
bcmv3d_read_poison(struct bcmv3d_read *s, int error)
{
	int ret;

	if (s == NULL || error <= 0 || error > MAX_ERRNO)
		return EINVAL;
	if (s->owner != s)
		return EPERM;
	if (s->object == NULL || s->fence == NULL)
		return EINVAL;
	if (s->poisoned)
		return EALREADY;
	s->poisoned = true;
	if (dma_fence_get_status(s->fence) != 0)
		return EALREADY;
	dma_fence_set_error(s->fence, -error);
	ret = dma_fence_signal(s->fence);
	return ret < 0 ? -ret : ret == 0 ? 0 : EIO;
}
