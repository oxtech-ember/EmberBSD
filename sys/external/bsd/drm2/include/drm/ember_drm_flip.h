/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted. */

#ifndef _DRM_EMBER_DRM_FLIP_H_
#define _DRM_EMBER_DRM_FLIP_H_

#include <sys/types.h>

struct dma_fence;
struct drm_crtc;
struct drm_pending_vblank_event;

/*
 * Wait up to one second for an optional producer fence. The caller owns
 * its reference and holds no modeset, reservation, or IRQ lock.
 * Return zero or a positive BSD errno.
 */
int	ember_drm_flip_wait_producer(struct dma_fence *);

/*
 * Consume the owned output event under event_lock. Supply the scanout
 * sequence and monotonic timestamp on success, or a positive BSD errno.
 * A closed DRM file is allowed. Failure signals the output fence and
 * cancels the event. This never releases scanout memory or its READ fence.
 */
void	ember_drm_flip_finish(struct drm_crtc *,
	    struct drm_pending_vblank_event **, uint64_t, uint64_t, int);

#endif
