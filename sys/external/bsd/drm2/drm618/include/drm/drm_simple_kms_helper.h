/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: virtgpu_display.c of dist618 (MIT),
 * drm_encoder.h of dist618 (MIT); the GPL original was not read.
 *
 * Only the encoder of the simple KMS helper: an encoder with no functions
 * of its own besides the destruction.  The simple display pipe
 * (drm_simple_display_pipe_*) is not provided.
 */

#ifndef _DRM618_DRM_SIMPLE_KMS_HELPER_H_
#define _DRM618_DRM_SIMPLE_KMS_HELPER_H_

#include <drm/drm_encoder.h>

struct drm_device;

/* drm_encoder_init with the destroy function drm_encoder_cleanup. */
int drm_simple_encoder_init(struct drm_device *dev,
    struct drm_encoder *encoder, int encoder_type);

#endif	/* _DRM618_DRM_SIMPLE_KMS_HELPER_H_ */
