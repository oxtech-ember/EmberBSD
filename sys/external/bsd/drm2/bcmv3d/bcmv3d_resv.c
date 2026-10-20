/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* AI-assisted native BO producer ordering. No GPU or display register access. */
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <linux/dma-fence.h>
#include <linux/dma-resv.h>
#include <linux/slab.h>
#include <drm/drm_gem.h>
#include "bcmv3d_resv.h"

/* Integer ordering is defined for unrelated native reservation pointers. */
static int
bcmv3d_resv_order(const void *left, const void *right)
{
	struct drm_gem_object * const *a = left, * const *b = right;
	uintptr_t x = (uintptr_t)(*a)->resv, y = (uintptr_t)(*b)->resv;

	return x < y ? -1 : x > y ? 1 : 0;
}

static int
bcmv3d_resv_wait(struct dma_fence *fence, long *remaining)
{
	long waited;
	int status;

	if (fence == NULL)
		return 0;
	waited = dma_fence_wait_timeout(fence, true, *remaining);
	if (waited <= 0)
		return waited == 0 ? ETIME : -waited;
	*remaining = waited;
	status = dma_fence_get_status(fence);
	return status > 0 ? 0 : status == 0 ? EIO : -status;
}

void
bcmv3d_resv_unlock(struct bcmv3d_resv *s)
{

	if (s == NULL || !s->locked)
		return;
	drm_gem_unlock_reservations(s->objects, (int)s->count, &s->acquire);
	s->objects = NULL;
	s->count = 0;
	s->locked = s->published = false;
}

int
bcmv3d_resv_prepare(struct bcmv3d_resv *s, struct drm_gem_object **objects,
    unsigned int count, long timeout)
{
	struct dma_fence *exclusive, **shared;
	struct drm_gem_object *temporary;
	unsigned int i, j, nshared;
	long remaining = timeout;
	int error, ret;

	if (s == NULL || objects == NULL || count == 0 ||
	    count > BCMV3D_RESV_MAX || timeout <= 0)
		return EINVAL;
	if (s->locked || s->objects != NULL || s->count != 0)
		return EBUSY;
	for (i = 0; i < count; i++) {
		if (objects[i] == NULL || objects[i]->resv == NULL)
			return EINVAL;
	}
	/* Fixed workspace: no allocation and bounded O(n log n) preflight. */
	if (kheapsort(objects, count, sizeof(*objects), bcmv3d_resv_order,
	    &temporary) != 0)
		return EIO;
	for (i = 1; i < count; i++)
		if (objects[i - 1]->resv == objects[i]->resv)
			return EINVAL;
	ret = drm_gem_lock_reservations(objects, (int)count, &s->acquire);
	if (ret != 0) {
		/* The common helper ends acquisition; its caller finalizes it. */
		ww_acquire_fini(&s->acquire);
		return ret < 0 ? -ret : EIO;
	}
	s->objects = objects;
	s->count = count;
	s->locked = true;
	s->published = false;
	for (i = 0; i < count; i++) {
		exclusive = NULL;
		shared = NULL;
		nshared = 0;
		ret = dma_resv_get_fences_rcu(objects[i]->resv, &exclusive,
		    &nshared, &shared);
		if (ret != 0) {
			error = ret < 0 ? -ret : EIO;
			goto fail;
		}
		error = bcmv3d_resv_wait(exclusive, &remaining);
		for (j = 0; error == 0 && j < nshared; j++)
			error = bcmv3d_resv_wait(shared[j], &remaining);
		dma_fence_put(exclusive);
		for (j = 0; j < nshared; j++)
			dma_fence_put(shared[j]);
		kfree(shared);
		if (error != 0)
			goto fail;
	}
	return 0;
fail:
	bcmv3d_resv_unlock(s);
	return error;
}

int
bcmv3d_resv_publish(struct bcmv3d_resv *s, struct dma_fence *fence)
{
	unsigned int i;

	if (s == NULL || !s->locked || s->objects == NULL || s->count == 0 ||
	    s->count > BCMV3D_RESV_MAX || fence == NULL)
		return EINVAL;
	if (s->published)
		return EALREADY;
	if (dma_fence_get_status(fence) != 0)
		return EINVAL;
	/* Validate the complete actual ownership tuple before the first change. */
	for (i = 0; i < s->count; i++) {
		if (s->objects[i] == NULL || s->objects[i]->resv == NULL ||
		    !dma_resv_held(s->objects[i]->resv) ||
		    dma_resv_locking_ctx(s->objects[i]->resv) != &s->acquire)
			return EPERM;
	}
	for (i = 0; i < s->count; i++)
		dma_resv_add_excl_fence(s->objects[i]->resv, fence);
	s->published = true;
	return 0;
}
