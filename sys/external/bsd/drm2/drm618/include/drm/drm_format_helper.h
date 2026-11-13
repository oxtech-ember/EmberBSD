/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD clean-room implementation of the interface used by
 * MIT-licensed consumers; written without access to the GPL original.
 *
 * Conversion of framebuffer contents between pixel formats and copies of a
 * clipped rectangle, used by drivers that scan out a format different from
 * the one clients render in.
 */

#ifndef _DRM618_DRM_FORMAT_HELPER_H_
#define _DRM618_DRM_FORMAT_HELPER_H_

#include <linux/gfp.h>
#include <linux/types.h>

struct drm_format_info;
struct drm_framebuffer;
struct drm_rect;
struct iosys_map;

/*
 * Scratch memory the converters may need between calls.  Keep one per user,
 * initialise it with drm_format_conv_state_init() or DRM_FORMAT_CONV_STATE_INIT
 * and free it with drm_format_conv_state_release().
 */
struct drm_format_conv_state {
	struct {
		void *mem;
		size_t size;
		bool preallocated;	/* mem is not ours to free or grow */
	} tmp;
};

#define DRM_FORMAT_CONV_STATE_INIT {					\
	.tmp = { .mem = NULL, .size = 0, .preallocated = false },	\
}

/* A state whose scratch memory is a fixed buffer of the caller. */
#define DRM_FORMAT_CONV_STATE_INIT_PREALLOCATED(_mem, _size) {		\
	.tmp = { .mem = (_mem), .size = (_size), .preallocated = true },\
}

void drm_format_conv_state_init(struct drm_format_conv_state *state);
void drm_format_conv_state_copy(struct drm_format_conv_state *state,
    const struct drm_format_conv_state *old_state);
void *drm_format_conv_state_reserve(struct drm_format_conv_state *state,
    size_t new_size, gfp_t flags);
void drm_format_conv_state_release(struct drm_format_conv_state *state);

/* Byte offset of the first row and column of clip in a plane. */
unsigned int drm_fb_clip_offset(unsigned int pitch,
    const struct drm_format_info *format, const struct drm_rect *clip);

void drm_fb_memcpy(struct iosys_map *dst, const unsigned int *dst_pitch,
    const struct iosys_map *src, const struct drm_framebuffer *fb,
    const struct drm_rect *clip);
void drm_fb_swab(struct iosys_map *dst, const unsigned int *dst_pitch,
    const struct iosys_map *src, const struct drm_framebuffer *fb,
    const struct drm_rect *clip, bool cached,
    struct drm_format_conv_state *state);

/*
 * Conversions of the clip of fb (read through src) to the format in the
 * name, written through dst with the given pitches.
 */
#define DRM_FB_CONVERTER(name)						\
void name(struct iosys_map *dst, const unsigned int *dst_pitch,		\
    const struct iosys_map *src, const struct drm_framebuffer *fb,	\
    const struct drm_rect *clip, struct drm_format_conv_state *state)

DRM_FB_CONVERTER(drm_fb_xrgb8888_to_rgb332);
DRM_FB_CONVERTER(drm_fb_xrgb8888_to_rgb565);
DRM_FB_CONVERTER(drm_fb_xrgb8888_to_rgb565be);
DRM_FB_CONVERTER(drm_fb_xrgb8888_to_xrgb1555);
DRM_FB_CONVERTER(drm_fb_xrgb8888_to_argb1555);
DRM_FB_CONVERTER(drm_fb_xrgb8888_to_rgba5551);
DRM_FB_CONVERTER(drm_fb_xrgb8888_to_rgb888);
DRM_FB_CONVERTER(drm_fb_xrgb8888_to_bgr888);
DRM_FB_CONVERTER(drm_fb_xrgb8888_to_argb8888);
DRM_FB_CONVERTER(drm_fb_xrgb8888_to_abgr8888);
DRM_FB_CONVERTER(drm_fb_xrgb8888_to_xbgr8888);
DRM_FB_CONVERTER(drm_fb_xrgb8888_to_bgrx8888);
DRM_FB_CONVERTER(drm_fb_xrgb8888_to_xrgb2101010);
DRM_FB_CONVERTER(drm_fb_xrgb8888_to_argb2101010);
DRM_FB_CONVERTER(drm_fb_xrgb8888_to_gray8);
DRM_FB_CONVERTER(drm_fb_argb8888_to_argb4444);
DRM_FB_CONVERTER(drm_fb_xrgb8888_to_mono);
DRM_FB_CONVERTER(drm_fb_xrgb8888_to_gray2);

/* Convert to dst_format whatever fb holds; 0, or -EINVAL if no converter. */
int drm_fb_blit(struct iosys_map *dst, const unsigned int *dst_pitch,
    uint32_t dst_format, const struct iosys_map *src,
    const struct drm_framebuffer *fb, const struct drm_rect *clip,
    struct drm_format_conv_state *state);

#endif	/* _DRM618_DRM_FORMAT_HELPER_H_ */
