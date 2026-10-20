/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _BCMV3D_READ_H_
#define _BCMV3D_READ_H_
#include <sys/types.h>
struct drm_gem_object;
struct dma_fence;
struct bcmv3d_read {
	const struct bcmv3d_read *owner;
	struct drm_gem_object *object;
	struct dma_fence *fence;
	bool poisoned;
};
/*
 * Private zeroed storage. Do not copy or move a live lease. The caller supplies
 * a live validated GEM reference and serializes every operation on this lease.
 * Process context. Do not hold an engine/native hardware-owner mutex or this
 * BO's reservation lock. DRM modeset locks follow the core locking rules.
 * Begin retains the BO, waits only for its prior writer, and publishes a shared
 * pending fence. timeout is a positive tick budget for the writer wait only.
 * Each lease has a distinct fence context, including the valid context zero.
 * Simultaneous readers cannot replace one another in the reservation list.
 * Errors use positive DRM errno, including internal ERESTARTSYS.
 */
int bcmv3d_read_begin(struct bcmv3d_read *, struct drm_gem_object *, long);
/*
 * Complete only after actual retirement and consumer DMA/map cleanup, or after
 * a known never-started cancellation. No hardware retirement is inferred here.
 * Signals without taking the reservation lock, then drops the retained BO/ref.
 * A writer may already hold that lock while waiting for this read. Idempotent
 * for zeroed/released storage. Consumer completion must run in process context.
 */
int bcmv3d_read_end(struct bcmv3d_read *);
/*
 * An uncertain reader signals a positive DRM error and retains its BO/fence.
 * Poison is sticky until reboot; end cannot release that storage. The actual
 * consumer must also keep all DMA mappings and hardware resources alive.
 */
int bcmv3d_read_poison(struct bcmv3d_read *, int);
#endif
