/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted. Bounded native A733 atomic worker. */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");
#include <sys/param.h>
#include <sys/container_of.h>
#include <sys/kmem.h>
#include <sys/systm.h>
#include <linux/completion.h>
#include <linux/dma-fence.h>
#include <linux/jiffies.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_file.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_vblank.h>
#include <arm/sunxi/sun60i_a733_drm_frame.h>
#include "sun60i_a733_drm.h"

struct a733_commit {
	struct work work;
	struct completion done;
	struct drm_atomic_state *state;
	bool nonblock;
};

/* Complete the output event. Never signal the retained scanout READ fence. */
static void
a733_commit_event(struct a733_drm *sc, struct drm_crtc_state *state,
    const struct sun60i_a733_de352_completion *done, int error)
{
	struct drm_pending_vblank_event *event = state->event;
	struct drm_pending_event *base;
	unsigned long flags;

	if (event == NULL)
		return;
	base = &event->base;
	spin_lock_irqsave(&sc->dev->event_lock, flags);
	state->event = NULL;
	if (error == 0) {
		event->pipe = drm_crtc_index(&sc->crtc);
		event->event.sequence = 0;
		event->event.tv_sec = done->observed_time_ns / 1000000000;
		event->event.tv_usec = (done->observed_time_ns % 1000000000) / 1000;
		drm_send_event_locked(sc->dev, base);
	} else {
		if (base->fence != NULL) {
			dma_fence_set_error(base->fence, -error);
			(void)dma_fence_signal(base->fence);
		}
		if (base->completion != NULL) {
			complete_all(base->completion);
			if (base->completion_release != NULL)
				base->completion_release(base->completion);
			base->completion = NULL;
			base->completion_release = NULL;
		}
	}
	spin_unlock_irqrestore(&sc->dev->event_lock, flags);
	if (error != 0)
		drm_event_cancel_free(sc->dev, base);
}

static int
a733_explicit_fence(struct dma_fence *fence)
{
	long waited;
	int status;

	if (fence == NULL)
		return 0;
	waited = dma_fence_wait_timeout(fence, false, msecs_to_jiffies(1000));
	if (waited <= 0)
		return waited < 0 ? (int)-waited : ETIMEDOUT;
	status = dma_fence_get_status(fence);
	return status > 0 ? 0 : status < 0 ? -status : EIO;
}

void
a733_drm_work(struct work *work, void *cookie)
{
	struct a733_commit *job = container_of(work, struct a733_commit, work);
	struct a733_drm *sc = cookie;
	struct drm_atomic_state *state = job->state;
	struct drm_plane_state *plane;
	struct drm_crtc_state *crtc;
	struct a733_drm_fb *fb;
	struct sun60i_a733_de352_completion done = {0};
	struct sun60i_a733_drm_frame frame;
	int error;

	plane = drm_atomic_get_new_plane_state(state, &sc->plane);
	crtc = drm_atomic_get_new_crtc_state(state, &sc->crtc);
	fb = container_of(plane->fb, struct a733_drm_fb, base);
	frame = (struct sun60i_a733_drm_frame){
		.format = plane->fb->format->format,
		.modifier = plane->fb->modifier,
		.width = plane->fb->width,
		.height = plane->fb->height,
		.pitch = plane->fb->pitches[0],
		.offset = plane->fb->offsets[0],
	};

	/* This worker holds no modeset, reservation or provider lock. */
	error = a733_explicit_fence(plane->fence);
	if (error == 0)
		error = sun60i_a733_drm_frame_submit(sc->client, &sc->native,
		    fb->object, &frame, &done);
	if (error != 0) {
		mutex_enter(&sc->lock);
		if (sc->fault == 0)
			sc->fault = error;
		mutex_exit(&sc->lock);
	}
	a733_commit_event(sc, crtc, &done, error);
	drm_atomic_helper_commit_hw_done(state);
	drm_atomic_helper_cleanup_planes(sc->dev, state);
	drm_atomic_helper_commit_cleanup_done(state);
	drm_atomic_state_put(state);
	mutex_enter(&sc->lock);
	sc->busy = false;
	mutex_exit(&sc->lock);
	if (job->nonblock) {
		destroy_completion(&job->done);
		kmem_free(job, sizeof(*job));
	} else {
		complete_all(&job->done);
	}
}

int
a733_drm_atomic_commit(struct drm_device *dev, struct drm_atomic_state *state,
    bool nonblock)
{
	struct a733_drm *sc = dev->dev_private;
	struct a733_commit *job;
	int error;

	if (state->async_update)
		return -EOPNOTSUPP;
	mutex_enter(&sc->lock);
	error = sc->fault != 0 ? -sc->fault : sc->busy ? -EBUSY : 0;
	if (error == 0)
		sc->busy = true;
	mutex_exit(&sc->lock);
	if (error != 0)
		return error;
	job = kmem_zalloc(sizeof(*job), KM_SLEEP);
	init_completion(&job->done);
	job->state = state;
	job->nonblock = nonblock;
	error = drm_atomic_helper_setup_commit(state, nonblock);
	if (error != 0)
		goto fail;
	error = drm_atomic_helper_prepare_planes(dev, state);
	if (error != 0)
		goto fail;
	/*
	 * One admitted job remains busy through cleanup_done. Prior work is
	 * done.
	 */
	error = drm_atomic_helper_swap_state(state, false);
	if (error != 0) {
		drm_atomic_helper_cleanup_planes(dev, state);
		goto fail;
	}
	drm_atomic_state_get(state);
	workqueue_enqueue(sc->queue, &job->work, NULL);
	if (!nonblock) {
		/* The worker does not acquire the caller's modeset locks. */
		wait_for_completion(&job->done);
		destroy_completion(&job->done);
		kmem_free(job, sizeof(*job));
	}
	/*
	 * The state is already swapped. Errors are in the output fence and
	 * fault.
	 */
	return 0;
fail:
	destroy_completion(&job->done);
	kmem_free(job, sizeof(*job));
	mutex_enter(&sc->lock);
	sc->busy = false;
	mutex_exit(&sc->lock);
	return error;
}

int
a733_drm_atomic_check(struct drm_device *dev, struct drm_atomic_state *state)
{
	struct a733_drm *sc = dev->dev_private;
	struct drm_crtc_state *crtc;
	struct drm_plane_state *plane;
	struct drm_connector_state *connector;
	const struct sun60i_a733_output_mode *mode = &sc->native.mode;
	int error;

	if (state->async_update)
		return -EOPNOTSUPP;
	mutex_enter(&sc->lock);
	error = sc->fault;
	mutex_exit(&sc->lock);
	if (error != 0)
		return -error;
	crtc = drm_atomic_get_crtc_state(state, &sc->crtc);
	if (IS_ERR(crtc))
		return PTR_ERR(crtc);
	plane = drm_atomic_get_plane_state(state, &sc->plane);
	if (IS_ERR(plane))
		return PTR_ERR(plane);
	connector = drm_atomic_get_connector_state(state, &sc->connector);
	if (IS_ERR(connector))
		return PTR_ERR(connector);
	/*
	 * No DE stop contract exists. Never accept disable as a stopped
	 * scanout.
	 */
	if (!crtc->enable || !crtc->active || plane->fb == NULL ||
	    plane->crtc != &sc->crtc || connector->crtc != &sc->crtc)
		return -EOPNOTSUPP;
	if (!a733_drm_mode_matches(mode, &crtc->mode) ||
	    plane->src_x != 0 || plane->src_y != 0 ||
	    plane->src_w != (mode->hdisplay << 16) ||
	    plane->src_h != (mode->vdisplay << 16) ||
	    plane->crtc_x != 0 || plane->crtc_y != 0 ||
	    plane->crtc_w != mode->hdisplay || plane->crtc_h != mode->vdisplay ||
	    plane->rotation != DRM_MODE_ROTATE_0 ||
	    plane->fb->width != mode->hdisplay || plane->fb->height != mode->vdisplay ||
	    plane->fb->format->format != DRM_FORMAT_XRGB8888 ||
	    plane->fb->modifier != DRM_FORMAT_MOD_LINEAR || plane->fb->offsets[0] != 0)
		return -EINVAL;
	return drm_atomic_helper_check(dev, state);
}
