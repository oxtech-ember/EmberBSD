/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* AI-assisted error completion for detached DRM atomic events. */
#include <sys/errno.h>
#include <sys/systm.h>

#include <linux/completion.h>
#include <linux/dma-fence.h>

#include <drm/drm_file.h>
#include <drm/drm_vblank.h>

#include "bcmv3d_kms_event.h"

void
bcmv3d_kms_event_fail(struct drm_device *dev,
    struct drm_pending_vblank_event *event, int error)
{
	struct completion *completion;
	void (*completion_release)(struct completion *);
	struct drm_pending_event *pending;

	if (event == NULL)
		return;
	if (error <= 0 || error > ELAST)
		error = EIO;
	pending = &event->base;
	if (pending->fence != NULL)
		dma_fence_set_error(pending->fence, -error);

	completion = pending->completion;
	completion_release = pending->completion_release;
	pending->completion = NULL;
	pending->completion_release = NULL;
	if (completion != NULL) {
		complete_all(completion);
		if (completion_release != NULL)
			completion_release(completion);
	}
	if (pending->fence != NULL)
		dma_fence_signal(pending->fence);

	/* cancel_free takes event_lock and drops the event's fence reference. */
	drm_event_cancel_free(dev, pending);
}
