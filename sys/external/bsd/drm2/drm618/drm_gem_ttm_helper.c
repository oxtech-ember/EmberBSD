/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD.  The GEM helpers of drivers
 * whose buffer objects are TTM objects (the object funcs print_info, vmap,
 * vunmap, mmap and the dumb_map_offset of a driver), written to the
 * interface the MIT driver sources use and to drm/drm_gem.h and
 * drm/ttm/ttm_bo.h.  The upstream file is GPL-2.0-or-later and was not read.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>

#include <linux/errno.h>

#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/drm_vma_manager.h>
#include <drm/ttm/ttm_bo.h>
#include <drm/ttm/ttm_tt.h>

#include <drm/drm_gem_ttm_helper.h>

/* The placement and the cache attribute of the object, one line. */
void
drm_gem_ttm_print_info(struct drm_printer *p, unsigned int indent,
    const struct drm_gem_object *gem)
{
	struct ttm_buffer_object *bo = drm_gem_ttm_of_gem(__UNCONST(gem));
	static const char *const names[] = { "system", "tt", "vram", "priv" };
	const struct ttm_resource *res = bo->resource;
	const char *place = "none";
	const char *cache = "n/a";

	if (res != NULL)
		place = res->mem_type < __arraycount(names) ?
		    names[res->mem_type] : "driver";
	if (bo->ttm != NULL) {
		switch (bo->ttm->caching) {
		case ttm_cached:
			cache = "cached";
			break;
		case ttm_write_combined:
			cache = "write-combined";
			break;
		case ttm_uncached:
			cache = "uncached";
			break;
		}
	}
	drm_printf_indent(p, indent, "placement=%s\n", place);
	drm_printf_indent(p, indent, "caching=%s\n", cache);
}

int
drm_gem_ttm_vmap(struct drm_gem_object *gem, struct iosys_map *map)
{

	return ttm_bo_vmap(drm_gem_ttm_of_gem(gem), map);
}

void
drm_gem_ttm_vunmap(struct drm_gem_object *gem, struct iosys_map *map)
{

	ttm_bo_vunmap(drm_gem_ttm_of_gem(gem), map);
}

/*
 * The user mapping of a TTM object is not provided: drm618_vma.c maps objects
 * whose mmap handler leaves vm_ops (or a remap_pfn_range) in the vma, and the
 * fault path of a TTM object is the uvm pager of ttm_bo_vm_bsd.c
 * (ttm_bo_uvm_create), which is not connected to it.  The call fails, it does
 * not map something else.
 */
int
drm_gem_ttm_mmap(struct drm_gem_object *gem __unused,
    struct vm_area_struct *vma __unused)
{

	return -EOPNOTSUPP;
}

/* The fake offset of the object in the device file: what mmap of the handle takes. */
int
drm_gem_ttm_dumb_map_offset(struct drm_file *file, struct drm_device *dev __unused,
    uint32_t handle, uint64_t *offset)
{
	struct drm_gem_object *gem;
	int ret;

	gem = drm_gem_object_lookup(file, handle);
	if (gem == NULL)
		return -ENOENT;
	ret = drm_gem_create_mmap_offset(gem);
	if (ret == 0)
		*offset = drm_vma_node_offset_addr(&gem->vma_node);
	drm_gem_object_put(gem);
	return ret;
}
