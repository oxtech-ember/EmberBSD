/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD.  Written to the interface the
 * MIT sources of the 6.18 drivers use (nouveau_gem.c, amdgpu_gem.c,
 * radeon_gem.c, qxl_object.c, vmwgfx_gem.c: the members of
 * struct drm_gem_object_funcs they fill and the one function they call).
 * The upstream header is GPL-2.0-or-later and was not read.
 */
#ifndef _DRM618_DRM_GEM_TTM_HELPER_H_
#define _DRM618_DRM_GEM_TTM_HELPER_H_

#include <linux/container_of.h>
#include <linux/iosys-map.h>

#include <drm/drm_device.h>
#include <drm/drm_gem.h>
#include <drm/drm_print.h>
#include <drm/ttm/ttm_bo.h>

/* The buffer object a GEM object is the base of. */
static inline struct ttm_buffer_object *
drm_gem_ttm_of_gem(struct drm_gem_object *gem_obj)
{

	return container_of(gem_obj, struct ttm_buffer_object, base);
}

void	drm_gem_ttm_print_info(struct drm_printer *p, unsigned int indent,
	    const struct drm_gem_object *gem);
int	drm_gem_ttm_vmap(struct drm_gem_object *gem, struct iosys_map *map);
void	drm_gem_ttm_vunmap(struct drm_gem_object *gem, struct iosys_map *map);
int	drm_gem_ttm_mmap(struct drm_gem_object *gem, struct vm_area_struct *vma);
int	drm_gem_ttm_dumb_map_offset(struct drm_file *file, struct drm_device *dev,
	    uint32_t handle, uint64_t *offset);

#endif
