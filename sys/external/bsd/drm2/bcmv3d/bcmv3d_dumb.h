/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _BCMV3D_DUMB_H_
#define _BCMV3D_DUMB_H_

struct drm_device;
struct drm_file;
struct drm_mode_create_dumb;

/* Generic CPU buffer creation. Returns zero or a negative DRM errno. */
int bcmv3d_dumb_create(struct drm_file *, struct drm_device *,
    struct drm_mode_create_dumb *);

#endif
