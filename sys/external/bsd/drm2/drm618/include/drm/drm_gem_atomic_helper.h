/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD clean-room implementation of the interface used by
 * MIT-licensed consumers; written without access to the GPL original.
 *
 * Atomic-commit helpers for GEM-backed planes: the implicit fence of the
 * buffer and the "shadow" plane state that maps the buffer for the CPU for
 * the duration of a commit.  Only drm_gem_plane_helper_prepare_fb() is
 * implemented (drm618_gem_atomic_helper.c); the shadow-plane functions are
 * declared for the drivers that name them in DRM_GEM_SHADOW_PLANE_FUNCS and
 * DRM_GEM_SHADOW_PLANE_HELPER_FUNCS.
 */

#ifndef _DRM618_DRM_GEM_ATOMIC_HELPER_H_
#define _DRM618_DRM_GEM_ATOMIC_HELPER_H_

#include <linux/container_of.h>
#include <linux/iosys-map.h>

#include <drm/drm_format_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_plane.h>

/*
 * struct drm_plane_helper_funcs.prepare_fb: attach the fence of the GEM
 * object behind new_state->fb to the state unless the state has one.
 */
int drm_gem_plane_helper_prepare_fb(struct drm_plane *plane,
    struct drm_plane_state *new_state);

/*
 * Plane state of a driver that reads the framebuffer with the CPU during
 * a commit: map[] holds the mappings made by begin_fb_access, data[] the
 * same addresses for the converters of drm_format_helper.h, fmtcnv_state
 * their scratch memory.
 */
struct drm_shadow_plane_state {
	struct drm_plane_state base;
	struct drm_format_conv_state fmtcnv_state;
	struct iosys_map map[DRM_FORMAT_MAX_PLANES];
	struct iosys_map data[DRM_FORMAT_MAX_PLANES];
};

static inline struct drm_shadow_plane_state *
to_drm_shadow_plane_state(struct drm_plane_state *state)
{

	return container_of(state, struct drm_shadow_plane_state, base);
}

/* struct drm_plane_funcs members for a drm_shadow_plane_state. */
void drm_gem_reset_shadow_plane(struct drm_plane *plane);
struct drm_plane_state *drm_gem_duplicate_shadow_plane_state(
    struct drm_plane *plane);
void drm_gem_destroy_shadow_plane_state(struct drm_plane *plane,
    struct drm_plane_state *plane_state);

/* struct drm_plane_helper_funcs members: map the buffer for one commit. */
int drm_gem_begin_shadow_fb_access(struct drm_plane *plane,
    struct drm_plane_state *plane_state);
void drm_gem_end_shadow_fb_access(struct drm_plane *plane,
    struct drm_plane_state *plane_state);

#define DRM_GEM_SHADOW_PLANE_FUNCS					\
	.reset = drm_gem_reset_shadow_plane,				\
	.atomic_duplicate_state = drm_gem_duplicate_shadow_plane_state,	\
	.atomic_destroy_state = drm_gem_destroy_shadow_plane_state

#define DRM_GEM_SHADOW_PLANE_HELPER_FUNCS				\
	.begin_fb_access = drm_gem_begin_shadow_fb_access,		\
	.end_fb_access = drm_gem_end_shadow_fb_access

#endif	/* _DRM618_DRM_GEM_ATOMIC_HELPER_H_ */
