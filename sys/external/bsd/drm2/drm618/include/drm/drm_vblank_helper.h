/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD clean-room implementation of the interface used by
 * MIT-licensed consumers; written without access to the GPL original.
 *
 * Helpers a driver plugs into struct drm_crtc_funcs and
 * struct drm_crtc_helper_funcs to run vblank from a timer.
 */

#ifndef _DRM618_DRM_VBLANK_HELPER_H_
#define _DRM618_DRM_VBLANK_HELPER_H_

#include <linux/ktime.h>
#include <linux/types.h>

struct drm_atomic_state;
struct drm_crtc;

/* struct drm_crtc_helper_funcs: send the event, switch vblank with the CRTC. */
void drm_crtc_vblank_atomic_flush(struct drm_crtc *crtc,
    struct drm_atomic_state *state);
void drm_crtc_vblank_atomic_enable(struct drm_crtc *crtc,
    struct drm_atomic_state *state);
void drm_crtc_vblank_atomic_disable(struct drm_crtc *crtc,
    struct drm_atomic_state *state);

/* struct drm_crtc_funcs: vblank driven by the timer of the CRTC. */
int drm_crtc_vblank_helper_enable_vblank_timer(struct drm_crtc *crtc);
void drm_crtc_vblank_helper_disable_vblank_timer(struct drm_crtc *crtc);
bool drm_crtc_vblank_helper_get_vblank_timestamp_from_timer(
    struct drm_crtc *crtc, int *max_error, ktime_t *vblank_time,
    bool in_vblank_irq);

/* Initialisers for the members of struct drm_crtc_helper_funcs. */
#define DRM_CRTC_HELPER_VBLANK_FUNCS					\
	.atomic_flush = drm_crtc_vblank_atomic_flush,			\
	.atomic_enable = drm_crtc_vblank_atomic_enable,			\
	.atomic_disable = drm_crtc_vblank_atomic_disable

/* Initialisers for the vblank members of struct drm_crtc_funcs. */
#define DRM_CRTC_VBLANK_TIMER_FUNCS					\
	.enable_vblank = drm_crtc_vblank_helper_enable_vblank_timer,	\
	.disable_vblank = drm_crtc_vblank_helper_disable_vblank_timer,	\
	.get_vblank_timestamp =						\
	    drm_crtc_vblank_helper_get_vblank_timestamp_from_timer

#endif	/* _DRM618_DRM_VBLANK_HELPER_H_ */
