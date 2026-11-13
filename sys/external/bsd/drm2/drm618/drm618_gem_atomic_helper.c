/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD.  Written to the contract in
 * the MIT header drm_plane.h (the documentation of drm_plane_state.fence) and
 * the dma_resv documentation; the GPL-only upstream drm_gem_atomic_helper.c
 * was not read.
 *
 * drm_gem_plane_helper_prepare_fb: the implicit fence of a plane update.
 * Drivers that use implicit fencing call it from prepare_fb: the fences
 * of the GEM objects of the framebuffer that must signal before the buffer
 * is scanned out (the write fences) become the fence of the plane state.  An
 * explicit fence given by user space is kept: user space then synchronises
 * by itself.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>

#include <linux/dma-fence.h>
#include <linux/dma-fence-unwrap.h>
#include <linux/dma-resv.h>
#include <linux/errno.h>

#include <drm/drm_framebuffer.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_plane.h>

int
drm_gem_plane_helper_prepare_fb(struct drm_plane *plane __unused,
    struct drm_plane_state *state)
{
	struct dma_fence *fence = NULL, *new, *merged;
	size_t i;
	int ret;

	if (state->fb == NULL)
		return 0;
	if (state->fence != NULL)
		return 0;	/* explicit fencing */

	for (i = 0; i < ARRAY_SIZE(state->fb->obj); i++) {
		struct drm_gem_object *obj = state->fb->obj[i];

		if (obj == NULL)
			continue;
		ret = dma_resv_get_singleton(obj->resv, DMA_RESV_USAGE_WRITE,
		    &new);
		if (ret) {
			dma_fence_put(fence);
			return ret;
		}
		if (new == NULL)
			continue;
		if (fence == NULL) {
			fence = new;
			continue;
		}
		merged = dma_fence_unwrap_merge(fence, new);
		dma_fence_put(fence);
		dma_fence_put(new);
		if (merged == NULL)
			return -ENOMEM;
		fence = merged;
	}
	state->fence = fence;
	return 0;
}
