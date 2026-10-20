/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* AI-assisted fixed-mode KMS on the retained native GEM/HVS owner. */
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <linux/completion.h>
#include <linux/dma-fence.h>
#include <linux/slab.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_drv.h>
#include <drm/drm_encoder.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_file.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_modeset_helper.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_vblank.h>
#include "bcmv3d_drv.h"
#include "bcmv3d_kms.h"
#include "bcmv3d_kms_event.h"
#include "bcmv3d_scanout.h"
#include "bcmv3d_vblank_native.h"

struct bcmv3d_kms_plane_state {
	struct drm_plane_state base;
	struct bcmv3d_scanout_frame *frame;
	struct dma_fence *explicit;
};

struct bcmv3d_kms {
	struct bcmv3d_device *sc;
	struct drm_plane plane;
	struct drm_crtc crtc;
	struct drm_encoder encoder;
	struct drm_connector connector;
	struct completion latch;
	struct drm_pending_vblank_event *event;
	bcmv3d_kms_detect_fn detect;
	void *detect_arg;
	uint64_t event_floor, vblank_sequence, latch_sequence;
	ktime_t latch_time;
	int fault, pending_error;
	bool bound, waiting, polling, vblank_on;
};

static const struct drm_display_mode bcmv3d_kms_mode = {
	.clock = 148500,
	.hdisplay = 1920, .hsync_start = 2008, .hsync_end = 2052, .htotal = 2200,
	.vdisplay = 1080, .vsync_start = 1084, .vsync_end = 1089, .vtotal = 1125,
	.flags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC,
	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED
};

static struct bcmv3d_kms_plane_state *
kms_plane_state(struct drm_plane_state *state)
{

	return container_of(state, struct bcmv3d_kms_plane_state, base);
}

/* Once detached, an event has exactly one terminal owner. No false flip. */
static void
kms_fault(struct bcmv3d_kms *k, int error)
{
	struct drm_device *dev = k->sc->drm;
	struct drm_pending_vblank_event *event;
	unsigned long flags;
	bool waiting;

	spin_lock_irqsave(&dev->event_lock, flags);
	if (k->fault == 0)
		k->fault = error > 0 && error <= ELAST ? error : EIO;
	k->pending_error = k->fault;
	event = k->event;
	k->event = NULL;
	waiting = k->waiting;
	k->waiting = false;
	spin_unlock_irqrestore(&dev->event_lock, flags);
	bcmv3d_kms_event_fail(dev, event, k->fault);
	if (waiting)
		complete_all(&k->latch);
}

static int
kms_available(struct bcmv3d_kms *k)
{
	unsigned long flags;
	int error;

	spin_lock_irqsave(&k->sc->drm->event_lock, flags);
	error = k->fault != 0 ? k->fault : !k->bound ? EAGAIN : 0;
	spin_unlock_irqrestore(&k->sc->drm->event_lock, flags);
	return error;
}

static bool
kms_mode_valid(const struct drm_display_mode *m)
{
	const struct drm_display_mode *fixed = &bcmv3d_kms_mode;

	return m->clock == fixed->clock && m->hdisplay == fixed->hdisplay &&
	    m->hsync_start == fixed->hsync_start && m->hsync_end == fixed->hsync_end &&
	    m->htotal == fixed->htotal && m->hskew == 0 &&
	    m->vdisplay == fixed->vdisplay && m->vsync_start == fixed->vsync_start &&
	    m->vsync_end == fixed->vsync_end && m->vtotal == fixed->vtotal &&
	    m->vscan == 0 && m->flags == fixed->flags;
}

static const struct drm_framebuffer_funcs kms_fb_funcs = {
	.destroy = drm_gem_fb_destroy,
	.create_handle = drm_gem_fb_create_handle
};

static struct drm_framebuffer *
kms_fb_create(struct drm_device *dev, struct drm_file *file,
    const struct drm_mode_fb_cmd2 *cmd)
{
	struct drm_framebuffer *fb;
	struct bcmv3d_bo *bo;
	unsigned int i;
	int error;

	if (cmd->width != 1920 || cmd->height != 1080 ||
	    cmd->pixel_format != DRM_FORMAT_XRGB8888 || cmd->pitches[0] != 7680 ||
	    cmd->offsets[0] != 0 || cmd->modifier[0] != DRM_FORMAT_MOD_LINEAR ||
	    (cmd->flags & ~DRM_MODE_FB_MODIFIERS) != 0)
		return ERR_PTR(-EINVAL);
	for (i = 1; i < 4; i++) {
		if (cmd->handles[i] != 0 || cmd->pitches[i] != 0 ||
		    cmd->offsets[i] != 0 || cmd->modifier[i] != 0)
			return ERR_PTR(-EINVAL);
	}
	fb = drm_gem_fb_create_with_funcs(dev, file, cmd, &kms_fb_funcs);
	if (IS_ERR(fb))
		return fb;
	error = bcmv3d_bo_reference(dev->dev_private, fb->obj[0], &bo);
	if (error != 0) {
		drm_framebuffer_put(fb);
		return ERR_PTR(-error);
	}
	drm_gem_object_put_unlocked(&bo->cma.base);
	return fb;
}

static void
kms_plane_cleanup(struct drm_plane *plane, struct drm_plane_state *state)
{
	struct bcmv3d_device *sc = plane->dev->dev_private;
	struct bcmv3d_kms_plane_state *s = kms_plane_state(state);
	struct bcmv3d_scanout_frame *frame = s->frame;
	int error;

	s->frame = NULL;
	dma_fence_put(s->explicit);
	s->explicit = NULL;
	if (frame != NULL) {
		/* Even a failed put may consume this reference. Never repeat it. */
		error = bcmv3d_scanout_put(frame);
		if (error != 0)
			kms_fault(sc->kms, error);
	}
}

static void
kms_plane_destroy_state(struct drm_plane *plane, struct drm_plane_state *state)
{

	kms_plane_cleanup(plane, state);
	__drm_atomic_helper_plane_destroy_state(state);
	kfree(kms_plane_state(state));
}

static void
kms_plane_reset(struct drm_plane *plane)
{
	struct bcmv3d_kms_plane_state *s;

	if (plane->state != NULL)
		kms_plane_destroy_state(plane, plane->state);
	plane->state = NULL;
	s = kzalloc(sizeof(*s), GFP_KERNEL);
	if (s != NULL)
		__drm_atomic_helper_plane_reset(plane, &s->base);
}

static struct drm_plane_state *
kms_plane_duplicate(struct drm_plane *plane)
{
	struct bcmv3d_kms_plane_state *s;

	if (plane->state == NULL)
		return NULL;
	s = kzalloc(sizeof(*s), GFP_KERNEL);
	if (s == NULL)
		return NULL;
	__drm_atomic_helper_plane_duplicate_state(plane, &s->base);
	/* Private resources are acquired by prepare_fb, never by state copying. */
	return &s->base;
}

static int
kms_plane_prepare(struct drm_plane *plane, struct drm_plane_state *state)
{
	struct bcmv3d_device *sc = plane->dev->dev_private;
	struct bcmv3d_kms_plane_state *s = kms_plane_state(state);
	struct drm_crtc_state *crtc;
	int error;

	if (s->frame != NULL || s->explicit != NULL)
		return -EBUSY;
	if (state->fb == NULL || state->crtc == NULL)
		return 0;
	crtc = drm_atomic_get_new_crtc_state(state->state, state->crtc);
	if (crtc == NULL || !crtc->active)
		return 0;
	error = kms_available(sc->kms);
	if (error != 0)
		return -error;
	s->explicit = dma_fence_get(state->fence);
	error = bcmv3d_scanout_reserve(sc->scanout, state->fb->obj[0], &s->frame);
	if (error != 0) {
		dma_fence_put(s->explicit);
		s->explicit = NULL;
	}
	return -error;
}

static int
kms_plane_check(struct drm_plane *plane, struct drm_plane_state *state)
{
	struct drm_crtc_state *crtc;
	int ret;

	(void)plane;
	if (state->crtc == NULL)
		return state->fb == NULL ? 0 : -EINVAL;
	crtc = drm_atomic_get_new_crtc_state(state->state, state->crtc);
	if (crtc == NULL)
		return -EINVAL;
	ret = drm_atomic_helper_check_plane_state(state, crtc,
	    DRM_PLANE_HELPER_NO_SCALING, DRM_PLANE_HELPER_NO_SCALING, false, true);
	if (ret != 0)
		return ret;
	if (!crtc->active)
		return 0;
	if (!state->visible)
		return -EINVAL;
	if (state->fb == NULL || state->fb->format->format != DRM_FORMAT_XRGB8888 ||
	    state->fb->modifier != DRM_FORMAT_MOD_LINEAR ||
	    state->rotation != DRM_MODE_ROTATE_0 || state->crtc_x != 0 ||
	    state->crtc_y != 0 || state->crtc_w != 1920 || state->crtc_h != 1080 ||
	    state->src_x != 0 || state->src_y != 0 ||
	    state->src_w != (1920U << 16) || state->src_h != (1080U << 16))
		return -EINVAL;
	return 0;
}

static const struct drm_plane_funcs kms_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = drm_plane_cleanup,
	.reset = kms_plane_reset,
	.atomic_duplicate_state = kms_plane_duplicate,
	.atomic_destroy_state = kms_plane_destroy_state
};
static const struct drm_plane_helper_funcs kms_plane_helpers = {
	.prepare_fb = kms_plane_prepare,
	.cleanup_fb = kms_plane_cleanup,
	.atomic_check = kms_plane_check
};

static uint32_t
kms_vblank_counter(struct drm_crtc *crtc)
{
	struct bcmv3d_device *sc = crtc->dev->dev_private;
	struct bcmv3d_scanout_status status;

	if (sc->scanout == NULL ||
	    bcmv3d_scanout_cached_status(sc->scanout, &status) != 0)
		return 0;
	/* Accumulate real HVS frame progression, including coalesced PV0 IRQs. */
	return (uint32_t)status.frame_epoch;
}

static int
kms_vblank_enable(struct drm_crtc *crtc)
{
	struct bcmv3d_device *sc = crtc->dev->dev_private;

	/* Native PV0 stays armed for real HVS retirement independently of DRM. */
	/* DRM calls this hook with event_lock held. Do not acquire it again. */
	return READ_ONCE(sc->kms->fault) != 0 ? -READ_ONCE(sc->kms->fault) :
	    READ_ONCE(sc->kms->bound) ? 0 : -EAGAIN;
}

static void
kms_vblank_disable(struct drm_crtc *crtc)
{

	(void)crtc;
	/* DRM accounting does not own the native retirement IRQ lifetime. */
}

static const struct drm_crtc_funcs kms_crtc_funcs = {
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.destroy = drm_crtc_cleanup,
	.reset = drm_atomic_helper_crtc_reset,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
	.get_vblank_counter = kms_vblank_counter,
	.enable_vblank = kms_vblank_enable,
	.disable_vblank = kms_vblank_disable
};

static enum drm_connector_status
kms_detect(struct drm_connector *connector, bool force)
{
	struct bcmv3d_kms *k = container_of(connector, struct bcmv3d_kms, connector);
	bool connected;
	int error;

	(void)force;
	if (kms_available(k) != 0)
		return connector_status_disconnected;
	error = k->detect(k->detect_arg, &connected);
	if (error != 0) {
		kms_fault(k, error);
		return connector_status_unknown;
	}
	return connected ? connector_status_connected : connector_status_disconnected;
}

static int
kms_get_modes(struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &bcmv3d_kms_mode);
	if (mode == NULL)
		return 0;
	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);
	return 1;
}

static enum drm_mode_status
kms_connector_mode_valid(struct drm_connector *connector,
    struct drm_display_mode *mode)
{

	(void)connector;
	return kms_mode_valid(mode) ? MODE_OK : MODE_BAD;
}

static struct drm_encoder *
kms_best_encoder(struct drm_connector *connector)
{
	struct bcmv3d_kms *k = container_of(connector, struct bcmv3d_kms, connector);

	return &k->encoder;
}

static const struct drm_encoder_funcs kms_encoder_funcs = {
	.destroy = drm_encoder_cleanup
};
static const struct drm_connector_funcs kms_connector_funcs = {
	.detect = kms_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state
};
static const struct drm_connector_helper_funcs kms_connector_helpers = {
	.get_modes = kms_get_modes,
	.mode_valid = kms_connector_mode_valid,
	.best_encoder = kms_best_encoder
};

static int
kms_atomic_check(struct drm_device *dev, struct drm_atomic_state *state)
{
	struct bcmv3d_device *sc = dev->dev_private;
	struct drm_crtc_state *crtc;
	struct drm_plane_state *plane;
	int error;

	error = kms_available(sc->kms);
	if (error != 0)
		return -error;
	if (state->async_update || state->legacy_cursor_update)
		return -EINVAL;
	crtc = drm_atomic_get_crtc_state(state, &sc->kms->crtc);
	if (IS_ERR(crtc))
		return PTR_ERR(crtc);
	plane = drm_atomic_get_plane_state(state, &sc->kms->plane);
	if (IS_ERR(plane))
		return PTR_ERR(plane);
	if (crtc->active && (!crtc->enable || !kms_mode_valid(&crtc->mode) ||
	    plane->fb == NULL || plane->crtc != &sc->kms->crtc))
		return -EINVAL;
	return drm_atomic_helper_check(dev, state);
}

/* Native callbacks run without owner locks. An older snapshot cannot flip. */
static void
kms_native_event(void *arg, uint64_t sequence, bool latched, int error)
{
	struct bcmv3d_kms *k = arg;
	struct drm_device *dev = k->sc->drm;
	unsigned long flags;
	bool count, complete;

	if (error != 0) {
		kms_fault(k, error);
		return;
	}
	spin_lock_irqsave(&dev->event_lock, flags);
	count = k->bound && k->fault == 0 && sequence > k->vblank_sequence;
	if (count)
		k->vblank_sequence = sequence;
	spin_unlock_irqrestore(&dev->event_lock, flags);
	if (count)
		drm_crtc_handle_vblank(&k->crtc);
	spin_lock_irqsave(&dev->event_lock, flags);
	complete = k->bound && k->fault == 0 && k->waiting && latched &&
	    sequence > k->event_floor;
	if (complete) {
		/* Capture the cooked sample now, not at later event delivery. */
		k->latch_sequence = drm_crtc_vblank_count_and_time(&k->crtc,
		    &k->latch_time);
		k->waiting = false;
		complete_all(&k->latch);
	}
	spin_unlock_irqrestore(&dev->event_lock, flags);
}

/* A successful event also makes the old buffer reusable by CPU producers. */
static void
kms_complete_event(struct bcmv3d_kms *k, struct drm_pending_vblank_event *event,
    uint64_t sequence, ktime_t time)
{
	struct drm_device *dev = k->sc->drm;
	struct timespec64 tv;
	unsigned long flags;
	int error;

	if (event == NULL)
		return;
	spin_lock_irqsave(&dev->event_lock, flags);
	error = k->fault;
	if (error == 0) {
		event->pipe = drm_crtc_index(&k->crtc);
		switch (event->event.base.type) {
		case DRM_EVENT_VBLANK:
		case DRM_EVENT_FLIP_COMPLETE:
			tv = ktime_to_timespec64(time);
			event->event.vbl.sequence = sequence;
			event->event.vbl.tv_sec = tv.tv_sec;
			event->event.vbl.tv_usec = tv.tv_nsec / 1000;
			break;
		case DRM_EVENT_CRTC_SEQUENCE:
			if (sequence != 0)
				event->event.seq.sequence = sequence;
			event->event.seq.time_ns = ktime_to_ns(time);
			break;
		}
		drm_send_event_locked(dev, &event->base);
	}
	spin_unlock_irqrestore(&dev->event_lock, flags);
	if (error != 0)
		bcmv3d_kms_event_fail(dev, event, error);
}

/* Detach terminal ownership before hw_done; deliver after old-state cleanup. */

static void
kms_commit_tail(struct drm_atomic_state *state)
{
	struct drm_device *dev = state->dev;
	struct bcmv3d_device *sc = dev->dev_private;
	struct bcmv3d_kms *k = sc->kms;
	struct drm_crtc_state *crtc = drm_atomic_get_new_crtc_state(state, &k->crtc);
	struct drm_plane_state *plane = drm_atomic_get_new_plane_state(state, &k->plane);
	struct bcmv3d_kms_plane_state *s = kms_plane_state(plane);
	struct drm_plane_state *old_plane = drm_atomic_get_old_plane_state(state,
	    &k->plane);
	struct bcmv3d_scanout_frame *old_frame = kms_plane_state(old_plane)->frame;
	struct drm_pending_vblank_event *unclaimed, *event;
	uint64_t latch_sequence;
	ktime_t latch_time;
	struct bcmv3d_vblank_snapshot snapshot;
	unsigned long flags;
	bool active = crtc->active, vblank_ref = false;
	int error;

	error = kms_available(k);
	drm_atomic_helper_update_legacy_modeset_state(dev, state);
	if (error == 0 && s->explicit != NULL) {
		error = dma_fence_get_status(s->explicit);
		error = error > 0 ? 0 : error == 0 ? EIO : -error;
	}
	if (error == 0 && active)
		error = s->frame == NULL ? EINVAL :
		    bcmv3d_scanout_finalize(s->frame, 5 * hz);
	if (error == 0)
		error = bcmv3d_vblank_native_snapshot(sc->vblank_native, &snapshot);
	if (error == 0 && snapshot.first_error != 0)
		error = snapshot.first_error;
	if (error == 0) {
		if (!k->vblank_on) {
			drm_crtc_vblank_on(&k->crtc);
			k->vblank_on = true;
		}
		error = -drm_crtc_vblank_get(&k->crtc);
		vblank_ref = error == 0;
	}
	if (error == 0) {
		reinit_completion(&k->latch);
		spin_lock_irqsave(&dev->event_lock, flags);
		if (k->waiting || k->event != NULL || k->fault != 0)
			error = k->fault != 0 ? k->fault : EBUSY;
		else {
			k->event = crtc->event;
			crtc->event = NULL;
			k->event_floor = snapshot.sequence;
			k->pending_error = 0;
			k->waiting = true;
		}
		spin_unlock_irqrestore(&dev->event_lock, flags);
	}
	if (error == 0)
		error = bcmv3d_scanout_submit(sc->scanout, active ? s->frame : NULL);
	if (error == 0 && wait_for_completion_timeout(&k->latch, 5 * hz) == 0)
		error = ETIMEDOUT;
	/* A page-flip event permits CPU reuse, not only GPU fence waits. */
	if (error == 0 && old_frame != NULL && old_frame != s->frame)
		error = bcmv3d_scanout_wait_retired(old_frame, 5 * hz);
	if (error != 0)
		kms_fault(k, error);
	/* An event not transferred before an error still has one terminal owner. */
	spin_lock_irqsave(&dev->event_lock, flags);
	unclaimed = crtc->event;
	crtc->event = NULL;
	event = k->event;
	k->event = NULL;
	k->waiting = false;
	latch_sequence = k->latch_sequence;
	latch_time = k->latch_time;
	error = k->pending_error != 0 ? k->pending_error : error;
	spin_unlock_irqrestore(&dev->event_lock, flags);
	if (unclaimed != NULL)
		bcmv3d_kms_event_fail(dev, unclaimed, error != 0 ? error : EIO);
	if (vblank_ref)
		drm_crtc_vblank_put(&k->crtc);
	if (!active && k->vblank_on) {
		drm_crtc_vblank_off(&k->crtc);
		k->vblank_on = false;
	}
	/* The native owner retains exposed BOs independently of plane-state refs. */
	drm_atomic_helper_commit_hw_done(state);
	drm_atomic_helper_cleanup_planes(dev, state);
	/* cleanup_fb can discover a late fault. Recheck before signalling success. */
	kms_complete_event(k, event, latch_sequence, latch_time);
}

static const struct drm_mode_config_funcs kms_mode_funcs = {
	.fb_create = kms_fb_create,
	.atomic_check = kms_atomic_check,
	.atomic_commit = drm_atomic_helper_commit
};
static const struct drm_mode_config_helper_funcs kms_mode_helpers = {
	.atomic_commit_tail = kms_commit_tail
};

int
bcmv3d_kms_init(struct bcmv3d_device *sc)
{
	static const uint32_t formats[] = { DRM_FORMAT_XRGB8888 };
	static const uint64_t modifiers[] = {
		DRM_FORMAT_MOD_LINEAR, DRM_FORMAT_MOD_INVALID
	};
	struct drm_device *dev = sc->drm;
	struct bcmv3d_kms *k;
	int ret;

	if (sc->kms != NULL)
		return EBUSY;
	k = kzalloc(sizeof(*k), GFP_KERNEL);
	if (k == NULL)
		return ENOMEM;
	k->sc = sc;
	sc->kms = k;
	init_completion(&k->latch);
	drm_mode_config_init(dev);
	dev->mode_config.funcs = &kms_mode_funcs;
	dev->mode_config.helper_private = &kms_mode_helpers;
	dev->mode_config.min_width = dev->mode_config.max_width = 1920;
	dev->mode_config.min_height = dev->mode_config.max_height = 1080;
	dev->mode_config.preferred_depth = 24;
	dev->mode_config.allow_fb_modifiers = true;
	ret = drm_universal_plane_init(dev, &k->plane, 1, &kms_plane_funcs,
	    formats, __arraycount(formats), modifiers, DRM_PLANE_TYPE_PRIMARY, "HVS0");
	if (ret != 0)
		goto fail;
	drm_plane_helper_add(&k->plane, &kms_plane_helpers);
	ret = drm_crtc_init_with_planes(dev, &k->crtc, &k->plane, NULL,
	    &kms_crtc_funcs, "PV0");
	if (ret != 0)
		goto fail;
	ret = drm_encoder_init(dev, &k->encoder, &kms_encoder_funcs,
	    DRM_MODE_ENCODER_TMDS, "HDMI0");
	if (ret != 0)
		goto fail;
	k->encoder.possible_crtcs = 1;
	ret = drm_connector_init(dev, &k->connector, &kms_connector_funcs,
	    DRM_MODE_CONNECTOR_HDMIA);
	if (ret != 0)
		goto fail;
	drm_connector_helper_add(&k->connector, &kms_connector_helpers);
	k->connector.polled = DRM_CONNECTOR_POLL_CONNECT | DRM_CONNECTOR_POLL_DISCONNECT;
	ret = drm_connector_attach_encoder(&k->connector, &k->encoder);
	if (ret != 0)
		goto fail;
	ret = drm_vblank_init(dev, 1);
	if (ret != 0)
		goto fail;
	dev->max_vblank_count = UINT32_MAX;
	drm_mode_config_reset(dev);
	if (k->plane.state == NULL || k->crtc.state == NULL ||
	    k->connector.state == NULL) {
		ret = -ENOMEM;
		goto fail;
	}
	return 0;
fail:
	bcmv3d_kms_fini(sc);
	return -ret;
}

void
bcmv3d_kms_fini(struct bcmv3d_device *sc)
{
	struct bcmv3d_kms *k = sc->kms;

	if (k == NULL)
		return;
	KASSERT(!k->bound && !k->waiting && k->event == NULL && !k->polling);
	drm_mode_config_cleanup(sc->drm);
	destroy_completion(&k->latch);
	sc->kms = NULL;
	kfree(k);
}

int
bcmv3d_kms_bind(struct bcmv3d_device *sc, bcmv3d_kms_detect_fn detect, void *arg)
{
	struct bcmv3d_kms *k = sc->kms;
	struct bcmv3d_scanout_status scanout;
	struct bcmv3d_vblank_snapshot irq;
	unsigned long flags;
	int error;

	if (k == NULL || detect == NULL || sc->scanout == NULL ||
	    sc->vblank_native == NULL)
		return EINVAL;
	if (k->bound || k->fault != 0)
		return EBUSY;
	error = bcmv3d_scanout_cached_status(sc->scanout, &scanout);
	if (error == 0)
		error = bcmv3d_vblank_native_snapshot(sc->vblank_native, &irq);
	if (error != 0)
		return error;
	if (scanout.first_error != 0 || irq.first_error != 0 ||
	    !irq.prepared || !irq.arm_attempted || !irq.routed ||
	    scanout.prepared != 0 || scanout.current_slot != -1 ||
	    scanout.pending_slot != -1 || irq.active_list != 32)
		return EPERM;
	k->detect = detect;
	k->detect_arg = arg;
	error = bcmv3d_scanout_bind(sc->scanout, kms_native_event, k);
	if (error != 0)
		return error;
	/* Finish initialization before any registered atomic client can enter. */
	drm_crtc_vblank_off(&k->crtc);
	spin_lock_irqsave(&sc->drm->event_lock, flags);
	if (k->fault != 0) {
		error = k->fault;
		spin_unlock_irqrestore(&sc->drm->event_lock, flags);
		return error;
	}
	k->bound = true;
	sc->drm->irq_enabled = true;
	spin_unlock_irqrestore(&sc->drm->event_lock, flags);
	drm_kms_helper_poll_init(sc->drm);
	k->polling = true;
	drm_kms_helper_hotplug_event(sc->drm);
	return 0;
}
