/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted. Bounded native A733 atomic worker. */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");
#include <sys/param.h>
#include <sys/container_of.h>
#include <sys/kmem.h>
#include <sys/atomic.h>
#include <sys/systm.h>
#include <sys/kernel.h>
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
	uint64_t owner_id;
	bool nonblock;
};

/* Complete the output event. Never signal the retained scanout READ fence. */
static void
a733_commit_event(struct a733_drm *sc, struct drm_pending_vblank_event *event,
    uint64_t sequence, uint64_t time_ns, int error)
{
	struct drm_pending_event *base;
	unsigned long flags;

	if (event == NULL)
		return;
	base = &event->base;
	spin_lock_irqsave(&sc->dev->event_lock, flags);
	if (error == 0) {
		event->pipe = drm_crtc_index(&sc->crtc);
		event->event.vbl.sequence = (uint32_t)sequence;
		event->event.vbl.tv_sec = time_ns / 1000000000;
		event->event.vbl.tv_usec = (time_ns % 1000000000) / 1000;
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

static int
a733_shadow_retain(void *cookie)
{
	struct a733_shadow_slot *slot = cookie;

	mutex_enter(&slot->sc->lock);
	KASSERT(slot->claimed && !slot->retained);
	slot->retained = true;
	mutex_exit(&slot->sc->lock);
	return 0;
}

static void
a733_shadow_release(void *cookie)
{
	struct a733_shadow_slot *slot = cookie;

	mutex_enter(&slot->sc->lock);
	KASSERT(slot->claimed && slot->retained);
	slot->retained = slot->claimed = false;
	mutex_exit(&slot->sc->lock);
}

static uint64_t
a733_event_owner(struct a733_drm *sc, const struct drm_crtc_state *crtc)
{
	const struct a733_drm_file *owner = NULL;
	uint64_t id = 0;
	unsigned long flags;

	spin_lock_irqsave(&sc->dev->event_lock, flags);
	if (crtc->event != NULL && crtc->event->base.file_priv != NULL)
		owner = crtc->event->base.file_priv->driver_priv;
	if (owner != NULL)
		id = owner->id;
	spin_unlock_irqrestore(&sc->dev->event_lock, flags);
	return id;
}

static int
a733_presented_stamp(struct a733_drm *sc,
    const struct sun60i_a733_de352_completion *confirmed,
    struct a733_vblank_stamp *stamp)
{
	struct sun60i_a733_de352_completion later = *confirmed;
	int error;

	if (a733_drm_vblank_stamp(sc, later.observed_sequence, stamp))
		return 0;
	for (unsigned i = 0; i < 2; i++) {
		uint64_t previous = later.observed_sequence;

		error = sun60i_a733_de352_observe_current(sc->client, &later);
		if (error != 0)
			return error;
		if (later.observed_sequence <= previous)
			return EPROTO;
		if (a733_drm_vblank_stamp(sc, later.observed_sequence, stamp))
			return 0;
	}
	return ETIMEDOUT;
}

static int
a733_submit_frame(struct a733_drm *sc, const struct a733_drm_fb *fb,
    const struct sun60i_a733_drm_frame *frame, bool has_in_fence,
    uint64_t owner, bool vblank_ref,
    struct sun60i_a733_de352_completion *done)
{
	struct drm_gem_cma_object *src = fb->object;
	struct a733_shadow_slot *slot = NULL;
	const bool imported = src->sgt != NULL;
	const bool cpu_dumb = !has_in_fence && !imported &&
	    owner != 0 && fb->owner_id == owner &&
	    src->a733_allocation_id == owner && src->base.dev == sc->dev &&
	    src->base.import_attach == NULL && src->base.resv == &src->base._resv;
	size_t bytes;
	int error;

	if (!sc->shadow_enabled || !vblank_ref || src->base.dev != sc->dev ||
	    !(cpu_dumb || (has_in_fence && imported &&
	    sun60i_a733_drm_frame_has_import_reader(src))))
		goto guarded;
	if (frame->height == 0 || frame->pitch >
	    (SIZE_MAX - (PAGE_SIZE - 1)) / frame->height)
		goto guarded;
	bytes = roundup((size_t)frame->pitch * frame->height, PAGE_SIZE);
	if (bytes == 0)
		goto guarded;
	mutex_enter(&sc->lock);
	if (!src->shadow_copying && (cpu_dumb ?
	    src->shadow_cpu_mapped && !src->a733_prime_exposed :
	    !src->shadow_cpu_mapped)) {
		for (unsigned i = 0; i < A733_DRM_SHADOW_SLOTS; i++) {
			if (!sc->shadows[i].claimed) {
				slot = &sc->shadows[i];
				slot->sc = sc;
				slot->claimed = true;
				src->shadow_copying = true;
				break;
			}
		}
	}
	mutex_exit(&sc->lock);
	if (slot == NULL)
		goto guarded;
	if (slot->object != NULL && slot->object->base.size < bytes) {
		drm_gem_object_put_unlocked(&slot->object->base);
		slot->object = NULL;
	}
	if (slot->object == NULL)
		slot->object = a733_drm_alloc_cma(sc, bytes);
	error = slot->object == NULL ? ENOMEM :
	    sun60i_a733_drm_frame_shadow_copy(&sc->native, src,
	    slot->object, frame);
	mutex_enter(&sc->lock);
	src->shadow_copying = false;
	if (error != 0)
		slot->claimed = false;
	mutex_exit(&sc->lock);
	if (error != 0)
		goto guarded;

	error = sun60i_a733_drm_frame_shadow_present(sc->client, &sc->native,
	    slot->object, frame, slot, a733_shadow_retain, a733_shadow_release,
	    done);
	mutex_enter(&sc->lock);
	/* An uncertain DMA attempt retains the claim until a proven stop. */
	if (!slot->retained)
		slot->claimed = false;
	mutex_exit(&sc->lock);
	return error;

guarded:
	return sun60i_a733_drm_frame_submit(sc->client, &sc->native, src, frame,
	    done);
}

void
a733_drm_work(struct work *work, void *cookie)
{
	struct a733_commit *job = container_of(work, struct a733_commit, work);
	struct a733_drm *sc = cookie;
	struct drm_atomic_state *state = job->state;
	struct drm_plane_state *plane;
	struct drm_crtc_state *crtc;
	struct drm_pending_vblank_event *event;
	unsigned long flags;
	struct sun60i_a733_de352_completion done = {0};
	uint64_t sequence = 0, time_ns = 0;
	ktime_t time;
	bool vblank_ref = false;
	int error;

	plane = drm_atomic_get_new_plane_state(state, &sc->plane);
	crtc = drm_atomic_get_new_crtc_state(state, &sc->crtc);

	/* This worker holds no modeset, reservation or provider lock. */
	error = crtc->active && plane->fb != NULL ?
	    a733_explicit_fence(plane->fence) : 0;
	/* A vblank reference keeps the DRM count current through the flip. */
	if (error == 0 && sc->vblank_irq) {
		if (!sc->vblank_on) {
			drm_crtc_vblank_on(&sc->crtc);
			a733_drm_vblank_history_reset(sc, true);
			sc->vblank_on = true;
		}
		vblank_ref = drm_crtc_vblank_get(&sc->crtc) == 0;
	}
	if (error == 0) {
		if (!crtc->active)
			error = sun60i_a733_de352_stop(sc->client, &done);
		else if (plane->fb == NULL)
			error = sun60i_a733_de352_disable_plane(sc->client, &done);
		else {
			struct a733_drm_fb * const fb =
			    container_of(plane->fb, struct a733_drm_fb, base);
			const struct sun60i_a733_drm_frame frame = {
				.format = plane->fb->format->format,
				.modifier = plane->fb->modifier,
				.width = plane->fb->width,
				.height = plane->fb->height,
				.pitch = plane->fb->pitches[0],
				.offset = plane->fb->offsets[0],
			};

			error = a733_submit_frame(sc, fb, &frame, plane->fence != NULL,
			    job->owner_id, vblank_ref, &done);
		}
	}
	if (error == 0 && vblank_ref && crtc->active && plane->fb != NULL &&
	    sc->shadow_enabled) {
		struct a733_vblank_stamp stamp;

		error = a733_presented_stamp(sc, &done, &stamp);
		if (error == 0) {
			sequence = stamp.sequence;
			time_ns = stamp.time_ns;
		}
	} else if (error == 0 && vblank_ref) {
		sequence = drm_crtc_vblank_count_and_time(&sc->crtc, &time);
		time_ns = (uint64_t)ktime_to_ns(time);
	} else if (error == 0) {
		/* Polled observation count without the vblank interrupt. */
		sequence = done.observed_sequence;
		time_ns = done.observed_time_ns;
	}
	if (error != 0) {
		mutex_enter(&sc->lock);
		if (sc->fault == 0)
			sc->fault = error;
		mutex_exit(&sc->lock);
	}
	if (vblank_ref)
		drm_crtc_vblank_put(&sc->crtc);
	if (error == 0 && !crtc->active && sc->vblank_on) {
		a733_drm_vblank_history_reset(sc, false);
		drm_crtc_vblank_off(&sc->crtc);
		sc->vblank_on = false;
	}
	/*
	 * Close can unlink the pending event under event_lock. The worker
	 * still owns it and its completion reference until send or cancel.
	 * Consume the CRTC pointer before hw_done, but deliver the event
	 * after cleanup and reopening admission for the next flip.
	 */
	spin_lock_irqsave(&sc->dev->event_lock, flags);
	event = crtc->event;
	crtc->event = NULL;
	spin_unlock_irqrestore(&sc->dev->event_lock, flags);
	drm_atomic_helper_commit_hw_done(state);
	drm_atomic_helper_cleanup_planes(sc->dev, state);
	drm_atomic_helper_commit_cleanup_done(state);
	drm_atomic_state_put(state);
	mutex_enter(&sc->lock);
	sc->busy = false;
	cv_broadcast(&sc->idle);
	mutex_exit(&sc->lock);
	a733_commit_event(sc, event, sequence, time_ns, error);
	if (job->nonblock) {
		destroy_completion(&job->done);
		kmem_free(job, sizeof(*job));
	} else {
		complete_all(&job->done);
	}
}

/* The selected removal stays current under the caller's modeset locks. */
static int
a733_drain(struct a733_drm *sc)
{
	const unsigned start = getticks(), budget = 3 * hz;

	while (sc->busy && sc->fault == 0) {
		const unsigned elapsed = (unsigned)getticks() - start;
		int error;

		if (elapsed >= budget)
			return ETIMEDOUT;
		error = cv_timedwait(&sc->idle, &sc->lock, budget - elapsed);
		if (error != 0 && error != EWOULDBLOCK)
			return error;
	}
	return sc->fault;
}

int
a733_drm_atomic_commit(struct drm_device *dev, struct drm_atomic_state *state,
    bool nonblock)
{
	struct a733_drm *sc = dev->dev_private;
	struct a733_commit *job;
	const struct drm_crtc_state *crtc;
	const struct drm_plane_state *plane, *old_plane;
	bool removal;
	int error;

	if (state->async_update)
		return -EOPNOTSUPP;
	crtc = drm_atomic_get_new_crtc_state(state, &sc->crtc);
	plane = drm_atomic_get_new_plane_state(state, &sc->plane);
	old_plane = drm_atomic_get_old_plane_state(state, &sc->plane);
	removal = !nonblock && ((crtc != NULL && !crtc->active) ||
	    (plane != NULL && plane->fb == NULL && old_plane != NULL &&
	    old_plane->fb != NULL));
	mutex_enter(&sc->lock);
	error = removal && sc->shadow_enabled ? -a733_drain(sc) :
	    sc->fault != 0 ? -sc->fault : sc->busy ? -EBUSY : 0;
	if (error == 0)
		sc->busy = true;
	mutex_exit(&sc->lock);
	if (error != 0)
		return error;
	job = kmem_zalloc(sizeof(*job), KM_SLEEP);
	init_completion(&job->done);
	job->state = state;
	job->nonblock = nonblock;
	if (sc->shadow_enabled && crtc != NULL && crtc->active) {
		job->owner_id = a733_event_owner(sc, crtc);
		/* Complete caller CPU stores before a worker reads the dumb BO. */
		membar_sync();
	}
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
	workqueue_enqueue(sc->queue, &job->work, sc->worker_cpu);
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
	cv_broadcast(&sc->idle);
	mutex_exit(&sc->lock);
	return error;
}

int
a733_drm_atomic_check(struct drm_device *dev, struct drm_atomic_state *state)
{
	struct a733_drm *sc = dev->dev_private;
	struct drm_crtc_state *crtc;
	const struct drm_plane_state *plane;
	const struct drm_connector_state *connector;
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
	if (!crtc->enable) {
		if (crtc->active || plane->fb != NULL || plane->crtc != NULL ||
		    connector->crtc != NULL)
			return -EINVAL;
		return drm_atomic_helper_check(dev, state);
	}
	if (connector->crtc != &sc->crtc)
		return -EOPNOTSUPP;
	if (!a733_drm_mode_matches(mode, &crtc->mode))
		return -EINVAL;
	if (plane->fb == NULL && plane->crtc == NULL)
		return drm_atomic_helper_check(dev, state);
	if (plane->fb == NULL || plane->crtc != &sc->crtc)
		return -EOPNOTSUPP;
	if (plane->src_x != 0 || plane->src_y != 0 ||
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
