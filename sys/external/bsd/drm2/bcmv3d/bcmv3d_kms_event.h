/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _BCMV3D_KMS_EVENT_H_
#define _BCMV3D_KMS_EVENT_H_

struct drm_device;
struct drm_pending_vblank_event;

/*
 * Cancel a detached atomic event after drm_atomic_helper_swap_state.
 * The caller must detach and clear its event pointer under dev->event_lock.
 * Call without event_lock. The caller transfers exclusive event ownership
 * exactly once. The event must not be armed on the vblank event list.
 * The atomic helper must have cleared commit->event during the state swap.
 * The associated CRTC state must have its event pointer cleared before state
 * destruction, so abort_completion cannot drop the completed reference twice.
 * An attached OUT_FENCE must be unsignaled and owned by this event.
 *
 * This is safe in process or interrupt context. It releases flip_done and its
 * completion reference, signals OUT_FENCE with an error, and cancels the event.
 * It does not send a page-flip event. NULL is a no-op. Other calls consume the
 * event; a stale pointer must not be reused. error is a positive BSD errno.
 * Values outside 1..ELAST, including ERESTARTSYS, become EIO to satisfy the
 * native dma_fence_set_error contract.
 */
void bcmv3d_kms_event_fail(struct drm_device *,
    struct drm_pending_vblank_event *, int);

#endif /* _BCMV3D_KMS_EVENT_H_ */
