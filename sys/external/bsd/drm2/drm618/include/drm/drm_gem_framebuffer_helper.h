/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD clean-room implementation of the interface used by
 * MIT-licensed consumers; written without access to the GPL original.
 *
 * Framebuffers whose planes are GEM objects: creation from user handles and
 * the matching struct drm_framebuffer_funcs members.
 */

#ifndef _DRM618_DRM_GEM_FRAMEBUFFER_HELPER_H_
#define _DRM618_DRM_GEM_FRAMEBUFFER_HELPER_H_

#include <linux/dma-direction.h>

struct drm_device;
struct drm_file;
struct drm_format_info;
struct drm_framebuffer;
struct drm_framebuffer_funcs;
struct drm_gem_object;
struct drm_mode_fb_cmd2;

/* The GEM object behind a plane of fb, or NULL if there is none. */
struct drm_gem_object *drm_gem_fb_get_obj(struct drm_framebuffer *fb,
    unsigned int plane);

/* struct drm_framebuffer_funcs members: drop the plane objects, make a handle. */
void drm_gem_fb_destroy(struct drm_framebuffer *fb);
int drm_gem_fb_create_handle(struct drm_framebuffer *fb,
    struct drm_file *file, unsigned int *handle);

/* struct drm_mode_config_funcs.fb_create implementations. */
struct drm_framebuffer *drm_gem_fb_create_with_funcs(struct drm_device *dev,
    struct drm_file *file, const struct drm_format_info *info,
    const struct drm_mode_fb_cmd2 *mode_cmd,
    const struct drm_framebuffer_funcs *funcs);
struct drm_framebuffer *drm_gem_fb_create(struct drm_device *dev,
    struct drm_file *file, const struct drm_format_info *info,
    const struct drm_mode_fb_cmd2 *mode_cmd);
struct drm_framebuffer *drm_gem_fb_create_with_dirty(struct drm_device *dev,
    struct drm_file *file, const struct drm_format_info *info,
    const struct drm_mode_fb_cmd2 *mode_cmd);

/* Make the CPU's view of all planes coherent around CPU access. */
int drm_gem_fb_begin_cpu_access(struct drm_framebuffer *fb,
    enum dma_data_direction dir);
void drm_gem_fb_end_cpu_access(struct drm_framebuffer *fb,
    enum dma_data_direction dir);

#endif	/* _DRM618_DRM_GEM_FRAMEBUFFER_HELPER_H_ */
