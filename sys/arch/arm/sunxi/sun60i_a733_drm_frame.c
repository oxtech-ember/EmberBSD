/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted. Native DRM scanout lifetime bridge. */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <linux/dma-fence.h>
#include <linux/dma-resv.h>
#include <linux/jiffies.h>
#include <linux/slab.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_cma_helper.h>
#include <arm/sunxi/sun60i_a733_de352_scanout.h>
#include <arm/sunxi/sun60i_a733_drm_frame.h>

struct a733_scanout {
	/* dma_fence_free() releases the containing allocation after RCU. */
	struct dma_fence fence;
	spinlock_t lock;
	struct drm_gem_object *gem;
	bool held, finished;
};
CTASSERT(offsetof(struct a733_scanout, fence) == 0);

static const char *
scanout_name(struct dma_fence *fence)
{
	(void)fence;
	return "a733-scanout";
}

static bool
scanout_enable(struct dma_fence *fence)
{
	(void)fence;
	/* The DE retirement callback signals this fence. */
	return true;
}

static void
scanout_free(struct dma_fence *fence)
{
	struct a733_scanout *s = container_of(fence, struct a733_scanout, fence);

	KASSERT(s->gem == NULL && !s->held && s->finished);
	spin_lock_destroy(&s->lock);
	dma_fence_free(fence);
}

static const struct dma_fence_ops scanout_ops = {
	.use_64bit_seqno = true,
	.get_driver_name = scanout_name,
	.get_timeline_name = scanout_name,
	.enable_signaling = scanout_enable,
	.release = scanout_free,
};

static int
scanout_retain(void *cookie)
{
	struct a733_scanout *s = cookie;
	int error = 0;

	spin_lock(&s->lock);
	if (s->held || s->finished)
		error = EINVAL;
	else {
		s->held = true;
		dma_fence_get(&s->fence);
	}
	spin_unlock(&s->lock);
	return error;
}

/* No reservation or provider lock is held by either caller of this helper. */
static void
scanout_finish(struct a733_scanout *s, bool release)
{
	struct drm_gem_object *gem = NULL;

	spin_lock(&s->lock);
	if (release) {
		KASSERT(s->held && !s->finished);
		s->held = false;
	}
	if (!s->held && !s->finished) {
		gem = s->gem;
		s->gem = NULL;
		s->finished = true;
		(void)dma_fence_signal_locked(&s->fence);
	}
	spin_unlock(&s->lock);
	/* Break GEM -> reservation -> fence -> GEM before the last fence put. */
	if (gem != NULL)
		drm_gem_object_put_unlocked(gem);
	if (release)
		dma_fence_put(&s->fence);
}

static void
scanout_release(void *cookie)
{
	scanout_finish(cookie, true);
}

static int
scanout_frame(const struct sun60i_a733_de352_state *state,
    struct drm_gem_cma_object *obj, const struct sun60i_a733_drm_frame *in,
    struct sun60i_a733_de352_frame *frame)
{
	uint64_t address, bytes;

	if (state == NULL || obj == NULL || in == NULL || frame == NULL ||
	    obj->base.resv == NULL || state->dmat == NULL ||
	    obj->dmat != state->dmat || obj->dmamap == NULL)
		return EINVAL;
	if (state->max_dma_address != UINT32_MAX ||
	    in->format != DRM_FORMAT_XRGB8888 ||
	    in->modifier != DRM_FORMAT_MOD_LINEAR || in->offset != 0)
		return EOPNOTSUPP;
	if (in->width == 0 || in->width > 1920 || in->height == 0 ||
	    in->height > 1080 || in->width != state->mode.hdisplay ||
	    in->height != state->mode.vdisplay ||
	    in->pitch < (uint64_t)in->width * 4 || (in->pitch & 63) != 0)
		return EINVAL;
	bytes = (uint64_t)in->pitch * in->height;
	if (obj->dmamap->dm_nsegs != 1 || bytes > obj->base.size ||
	    bytes > obj->dmasize || bytes > obj->dmamap->dm_mapsize ||
	    bytes > obj->dmamap->dm_segs[0].ds_len)
		return ERANGE;
	address = obj->dmamap->dm_segs[0].ds_addr;
	if ((address & 63) != 0 || address > UINT32_MAX ||
	    bytes - 1 > UINT32_MAX - address)
		return ERANGE;
	*frame = (struct sun60i_a733_de352_frame) {
		.dmat = obj->dmat, .map = obj->dmamap,
		.bytes = MIN(obj->base.size, obj->dmasize),
		.width = in->width, .height = in->height, .pitch = in->pitch,
	};
	return 0;
}

/* Recheck the current writer under the same lock that publishes our reader. */
static int
scanout_publish(struct a733_scanout *s, struct dma_resv *resv)
{
	struct dma_fence *writer;
	long waited;
	int error, status;

	for (u_int attempt = 0; attempt < 32; attempt++) {
		error = -dma_resv_lock_interruptible(resv, NULL);
		if (error != 0)
			return error;
		writer = dma_fence_get(dma_resv_get_excl(resv));
		status = writer == NULL ? 1 : dma_fence_get_status(writer);
		if (status > 0) {
			error = -dma_resv_reserve_shared(resv, 1);
			if (error == 0)
				dma_resv_add_shared_fence(resv, &s->fence);
			dma_resv_unlock(resv);
			dma_fence_put(writer);
			return error;
		}
		dma_resv_unlock(resv);
		if (status < 0) {
			dma_fence_put(writer);
			return -status;
		}
		/* A pending producer must not stall the display worker forever. */
		waited = dma_fence_wait_timeout(writer, true,
		    msecs_to_jiffies(1000));
		dma_fence_put(writer);
		if (waited <= 0)
			return waited < 0 ? (int)-waited : ETIMEDOUT;
	}
	return EAGAIN;
}

int
sun60i_a733_drm_frame_submit(struct sun60i_a733_de352_client *client,
    const struct sun60i_a733_de352_state *state,
    struct drm_gem_cma_object *obj, const struct sun60i_a733_drm_frame *in,
    struct sun60i_a733_de352_completion *completion)
{
	struct sun60i_a733_de352_completion done;
	struct sun60i_a733_de352_frame frame;
	struct a733_scanout *s;
	int error;

	if (client == NULL || completion == NULL)
		return EINVAL;
	error = scanout_frame(state, obj, in, &frame);
	if (error != 0)
		return error;
	s = kzalloc(sizeof(*s), GFP_KERNEL);
	if (s == NULL)
		return ENOMEM;
	spin_lock_init(&s->lock);
	dma_fence_init(&s->fence, &scanout_ops, &s->lock,
	    dma_fence_context_alloc(1), 1);
	drm_gem_object_get(&obj->base);
	s->gem = &obj->base;
	frame.cookie = s;
	frame.retain = scanout_retain;
	frame.release = scanout_release;
	error = scanout_publish(s, obj->base.resv);
	if (error == 0)
		error = sun60i_a733_de352_submit(client, &frame, &done);
	/* A retained provider reference prevents this path from ending DMA use. */
	scanout_finish(s, false);
	dma_fence_put(&s->fence);
	if (error == 0)
		*completion = done;
	return error;
}
