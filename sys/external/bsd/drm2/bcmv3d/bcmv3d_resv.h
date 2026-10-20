/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _BCMV3D_RESV_H_
#define _BCMV3D_RESV_H_
#include <sys/types.h>
#include <linux/ww_mutex.h>

/* Software policy: at most 48 KiB of CL/CSD list metadata per request. */
#define BCMV3D_RESV_MAX 4096
struct drm_gem_object;
struct dma_fence;

struct bcmv3d_resv {
	struct ww_acquire_ctx acquire;
	struct drm_gem_object **objects;
	unsigned int count;
	bool locked, published;
};

/*
 * Process context. Supply zeroed private storage and live GEM references.
 * The unique objects/reservations and the pointer array stay alive until unlock.
 * Prepare may reorder this private pointer array during duplicate preflight.
 * Lock order: reservations, then engine. Never wait with the engine locked.
 * Prepare locks all reservations and waits for prior exclusive/shared fences.
 * timeout is a positive tick budget shared by all waits, not by lock acquisition.
 * Complete prior fences without reservation locks. This helper owns no BO refs.
 * Errors use positive DRM errno, including its internal ERESTARTSYS restart code.
 * The existing DRM ioctl adapter translates that code for the native syscall.
 * Failed prepare releases every lock and its acquire context.
 */
int bcmv3d_resv_prepare(struct bcmv3d_resv *, struct drm_gem_object **,
    unsigned int, long);
/*
 * Publish one pending producer fence to all locked BOs. No GPU operation.
 * The caller holds a fence reference and controls its actual producer. Keep it
 * pending until publication, submission and producer completion. Complete it
 * without reservation locks. Actual reservation ownership is checked first.
 */
int bcmv3d_resv_publish(struct bcmv3d_resv *, struct dma_fence *);
/* Idempotent for zeroed or released storage. Must run on the acquiring thread. */
void bcmv3d_resv_unlock(struct bcmv3d_resv *);
#endif
