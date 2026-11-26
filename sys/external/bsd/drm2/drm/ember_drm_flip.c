/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted. Completion for single-plane KMS workers. */

#include <sys/param.h>
#include <sys/errno.h>

#include <linux/completion.h>
#include <linux/dma-fence.h>
#include <linux/jiffies.h>

#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <drm/drm_vblank.h>
#include <drm/ember_drm_flip.h>

void
ember_drm_flip_finish(struct drm_crtc *crtc,
    struct drm_pending_vblank_event **slot, uint64_t sequence, uint64_t time_ns,
    int error)
{
	struct drm_device *dev = crtc->dev;
	struct drm_pending_vblank_event *event;
	struct drm_pending_event *base;
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);
	event = *slot;
	*slot = NULL;
	if (event == NULL) {
		spin_unlock_irqrestore(&dev->event_lock, flags);
		return;
	}
	base = &event->base;
	if (error == 0) {
		event->pipe = drm_crtc_index(crtc);
		event->event.vbl.sequence = (uint32_t)sequence;
		event->event.vbl.tv_sec = time_ns / 1000000000;
		event->event.vbl.tv_usec = (time_ns % 1000000000) / 1000;
		drm_send_event_locked(dev, base);
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
	spin_unlock_irqrestore(&dev->event_lock, flags);
	if (error != 0)
		drm_event_cancel_free(dev, base);
}

int
ember_drm_flip_wait_producer(struct dma_fence *fence)
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
