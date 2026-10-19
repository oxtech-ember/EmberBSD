/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted. Native DRM scanout lifetime bridge. */
#ifndef _SUN60I_A733_DRM_FRAME_H
#define _SUN60I_A733_DRM_FRAME_H

struct drm_gem_cma_object;
struct sun60i_a733_de352_client;
struct sun60i_a733_de352_state;
struct sun60i_a733_de352_completion;

struct sun60i_a733_drm_frame {
	uint32_t format, width, height, pitch, offset;
	uint64_t modifier;
};

/*
 * Call from a sleepable worker without DRM, reservation or provider locks.
 * The caller owns a GEM reference. The object must belong to this CMA driver.
 * Publish a pending shared fence before DE can read the frame. The fence
 * remains pending until the provider proves retirement or no DMA attempt.
 * A failed DMA attempt retains the GEM object and fence until reboot.
 * The completion output changes only on success. Return native errno values.
 */
int sun60i_a733_drm_frame_submit(struct sun60i_a733_de352_client *,
    const struct sun60i_a733_de352_state *, struct drm_gem_cma_object *,
    const struct sun60i_a733_drm_frame *,
    struct sun60i_a733_de352_completion *);
#endif
