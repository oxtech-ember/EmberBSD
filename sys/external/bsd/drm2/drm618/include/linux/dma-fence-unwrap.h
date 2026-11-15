/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: virtgpu_submit.c of dist618 (MIT), the
 * unwrap header of the shared layer; the GPL header of Linux was not read.
 *
 * Shadow of <linux/dma-fence-unwrap.h> for the 6.18 core: the shared layer's
 * unwrap and the test the virtio-gpu submission makes on a fence.
 */

#ifndef _DRM618_LINUX_DMA_FENCE_UNWRAP_H_
#define _DRM618_LINUX_DMA_FENCE_UNWRAP_H_

#include_next <linux/dma-fence-unwrap.h>

/*
 * Is every leaf fence behind fence (a chain, an array, or a plain fence) of
 * the given context?  A fence that is already signalled is not looked at by
 * the unwrap and so does not count against.
 */
static inline bool
dma_fence_match_context(struct dma_fence *fence, u64 context)
{
	struct dma_fence_unwrap iter;
	struct dma_fence *tmp;

	dma_fence_unwrap_for_each(tmp, &iter, fence) {
		if (tmp->context != context) {
			dma_fence_put(iter.chain);
			return false;
		}
	}
	return true;
}

#endif	/* _DRM618_LINUX_DMA_FENCE_UNWRAP_H_ */
