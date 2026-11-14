/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: drm618/include/drm/drm_gem_framebuffer_helper.h
 * (the interface), drm_framebuffer.h, drm_fourcc.h, drm_gem.h,
 * drm_modeset_helper.h and drm_modeset_helper.c, drm_framebuffer.c (MIT);
 * the GPL-only upstream drm_gem_framebuffer_helper.c was not read.
 *
 * Framebuffers whose planes are GEM objects: creation from the handles of a
 * user's ADDFB2, the struct drm_framebuffer_funcs members that go with it, and
 * the CPU access bracket.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>

#include <linux/dma-direction.h>
#include <linux/err.h>
#include <linux/slab.h>

#include <drm/drm_damage_helper.h>
#include <drm/drm_file.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_modeset_helper.h>

struct drm_gem_object *
drm_gem_fb_get_obj(struct drm_framebuffer *fb, unsigned int plane)
{

	if (plane >= ARRAY_SIZE(fb->obj))
		return NULL;
	return fb->obj[plane];
}

void
drm_gem_fb_destroy(struct drm_framebuffer *fb)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(fb->obj); i++)
		drm_gem_object_put(fb->obj[i]);
	drm_framebuffer_cleanup(fb);
	kfree(fb);
}

int
drm_gem_fb_create_handle(struct drm_framebuffer *fb, struct drm_file *file,
    unsigned int *handle)
{

	if (fb->obj[0] == NULL)
		return -ENODEV;
	return drm_gem_handle_create(file, fb->obj[0], handle);
}

/*
 * Every plane of the framebuffer needs an object that holds the last row of
 * the plane: (height - 1) * pitch + the bytes of one row + the offset.  The
 * sum is made in 64 bits: pitch and offset come from user space.
 */
static int
gem_fb_check_size(const struct drm_format_info *info,
    const struct drm_mode_fb_cmd2 *mode_cmd, unsigned int plane,
    const struct drm_gem_object *obj)
{
	unsigned int width = drm_format_info_plane_width(info,
	    mode_cmd->width, plane);
	unsigned int height = drm_format_info_plane_height(info,
	    mode_cmd->height, plane);
	uint64_t min_size;

	if (height == 0)
		return -EINVAL;
	min_size = (uint64_t)(height - 1) * mode_cmd->pitches[plane] +
	    drm_format_info_min_pitch(info, plane, width) +
	    mode_cmd->offsets[plane];
	if (obj->size < min_size)
		return -EINVAL;
	return 0;
}

struct drm_framebuffer *
drm_gem_fb_create_with_funcs(struct drm_device *dev, struct drm_file *file,
    const struct drm_format_info *info,
    const struct drm_mode_fb_cmd2 *mode_cmd,
    const struct drm_framebuffer_funcs *funcs)
{
	struct drm_framebuffer *fb;
	struct drm_gem_object *obj;
	unsigned int i;
	int ret;

	if (info == NULL || info->num_planes == 0 ||
	    info->num_planes > ARRAY_SIZE(fb->obj))
		return ERR_PTR(-EINVAL);
	fb = kzalloc(sizeof(*fb), GFP_KERNEL);
	if (fb == NULL)
		return ERR_PTR(-ENOMEM);
	drm_helper_mode_fill_fb_struct(dev, fb, info, mode_cmd);

	for (i = 0; i < info->num_planes; i++) {
		obj = drm_gem_object_lookup(file, mode_cmd->handles[i]);
		if (obj == NULL) {
			ret = -ENOENT;
			goto err_put;
		}
		fb->obj[i] = obj;
		ret = gem_fb_check_size(info, mode_cmd, i, obj);
		if (ret)
			goto err_put;
	}
	ret = drm_framebuffer_init(dev, fb, funcs);
	if (ret)
		goto err_put;
	return fb;

err_put:
	for (i = 0; i < ARRAY_SIZE(fb->obj); i++)
		drm_gem_object_put(fb->obj[i]);
	kfree(fb);
	return ERR_PTR(ret);
}

static const struct drm_framebuffer_funcs gem_fb_funcs = {
	.destroy	= drm_gem_fb_destroy,
	.create_handle	= drm_gem_fb_create_handle,
};

static const struct drm_framebuffer_funcs gem_fb_funcs_dirty = {
	.destroy	= drm_gem_fb_destroy,
	.create_handle	= drm_gem_fb_create_handle,
	.dirty		= drm_atomic_helper_dirtyfb,
};

struct drm_framebuffer *
drm_gem_fb_create(struct drm_device *dev, struct drm_file *file,
    const struct drm_format_info *info,
    const struct drm_mode_fb_cmd2 *mode_cmd)
{

	return drm_gem_fb_create_with_funcs(dev, file, info, mode_cmd,
	    &gem_fb_funcs);
}

struct drm_framebuffer *
drm_gem_fb_create_with_dirty(struct drm_device *dev, struct drm_file *file,
    const struct drm_format_info *info,
    const struct drm_mode_fb_cmd2 *mode_cmd)
{

	return drm_gem_fb_create_with_funcs(dev, file, info, mode_cmd,
	    &gem_fb_funcs_dirty);
}

/*
 * The CPU access bracket concerns the memory of imported buffers; the dma-buf
 * of the 6.18 core has no begin/end of CPU access for another exporter (see
 * the README), so an imported plane is an explicit error, not a silent pass.
 * Objects of the driver itself are coherent with the CPU.
 */
int
drm_gem_fb_begin_cpu_access(struct drm_framebuffer *fb,
    enum dma_data_direction dir __unused)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(fb->obj); i++)
		if (fb->obj[i] != NULL && drm_gem_is_imported(fb->obj[i]))
			return -EOPNOTSUPP;
	return 0;
}

void
drm_gem_fb_end_cpu_access(struct drm_framebuffer *fb __unused,
    enum dma_data_direction dir __unused)
{
}
