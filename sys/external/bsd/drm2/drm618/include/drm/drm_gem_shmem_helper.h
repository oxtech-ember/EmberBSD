/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: virtgpu_object.c, virtgpu_plane.c,
 * virtgpu_drv.h, virtgpu_vq.c, virtgpu_prime.c of dist618 (MIT), drm_gem.h,
 * drm_gem_framebuffer_helper.h, drm_prime.h, drm_drv.h of dist618 (MIT),
 * efidrm618.c (the GEM object over a shmem file of this tree); the GPL
 * original was not read.
 *
 * GEM objects whose storage is a shmem file: the helper of the interface the
 * virtio-gpu driver calls (create, free, pages, sg table, kernel mapping,
 * user mapping).  Only what the drivers of this tree call exists; not here:
 * the madvise list, import of an sg table (drm_gem_shmem_prime_import_*),
 * dumb buffers (drm_gem_shmem_dumb_create), write-combined mappings.
 *
 * The pages of an object are held while something needs them: a pin
 * (drm_gem_shmem_pin), the kernel mapping, the cached sg table (until the
 * object dies), a user mapping (once for the mapping, once more for each
 * vm_ops open).  The fault handler never allocates.
 */

#ifndef _DRM618_DRM_GEM_SHMEM_HELPER_H_
#define _DRM618_DRM_GEM_SHMEM_HELPER_H_

#include <linux/container_of.h>
#include <linux/iosys-map.h>
#include <linux/mutex.h>
#include <linux/scatterlist.h>

#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/drm_print.h>

struct drm_device;
struct vm_area_struct;
struct vm_operations_struct;

/* The first member of a driver's object that is a shmem object must be this one. */
struct drm_gem_shmem_object {
	struct drm_gem_object	base;
	struct page		**pages;
	unsigned int		pages_use_count;	/* holders of pages */
	struct sg_table		*sgt;			/* cached, holds a use */
	void			*vaddr;			/* kernel mapping */
	unsigned int		vmap_use_count;		/* holds one page use */
	bool			map_wc;			/* accepted, not honoured */
	struct mutex		pages_lock;
};

#define	to_drm_gem_shmem_obj(obj)					\
	container_of(obj, struct drm_gem_shmem_object, base)

struct drm_gem_shmem_object *drm_gem_shmem_create(struct drm_device *dev,
    size_t size);
void drm_gem_shmem_free(struct drm_gem_shmem_object *shmem);

int drm_gem_shmem_pin(struct drm_gem_shmem_object *shmem);
void drm_gem_shmem_unpin(struct drm_gem_shmem_object *shmem);
int drm_gem_shmem_vmap(struct drm_gem_shmem_object *shmem,
    struct iosys_map *map);
void drm_gem_shmem_vunmap(struct drm_gem_shmem_object *shmem,
    struct iosys_map *map);
int drm_gem_shmem_mmap(struct drm_gem_shmem_object *shmem,
    struct vm_area_struct *vma);
/* A new table of the pinned pages; the caller frees it. */
struct sg_table *drm_gem_shmem_get_sg_table(struct drm_gem_shmem_object *shmem);
/* The table of the object, made once and kept until the object dies. */
struct sg_table *drm_gem_shmem_get_pages_sgt(struct drm_gem_shmem_object *shmem);
void drm_gem_shmem_print_info(const struct drm_gem_shmem_object *shmem,
    struct drm_printer *p, unsigned int indent);

extern const struct vm_operations_struct drm_gem_shmem_vm_ops;

/* The members of struct drm_gem_object_funcs for a shmem object. */
static inline void
drm_gem_shmem_object_free(struct drm_gem_object *obj)
{

	drm_gem_shmem_free(to_drm_gem_shmem_obj(obj));
}

static inline void
drm_gem_shmem_object_print_info(struct drm_printer *p, unsigned int indent,
    const struct drm_gem_object *obj)
{

	drm_gem_shmem_print_info(
	    to_drm_gem_shmem_obj((struct drm_gem_object *)__UNCONST(obj)),
	    p, indent);
}

static inline int
drm_gem_shmem_object_pin(struct drm_gem_object *obj)
{

	return drm_gem_shmem_pin(to_drm_gem_shmem_obj(obj));
}

static inline void
drm_gem_shmem_object_unpin(struct drm_gem_object *obj)
{

	drm_gem_shmem_unpin(to_drm_gem_shmem_obj(obj));
}

static inline struct sg_table *
drm_gem_shmem_object_get_sg_table(struct drm_gem_object *obj)
{

	return drm_gem_shmem_get_sg_table(to_drm_gem_shmem_obj(obj));
}

static inline int
drm_gem_shmem_object_vmap(struct drm_gem_object *obj, struct iosys_map *map)
{

	return drm_gem_shmem_vmap(to_drm_gem_shmem_obj(obj), map);
}

static inline void
drm_gem_shmem_object_vunmap(struct drm_gem_object *obj, struct iosys_map *map)
{

	drm_gem_shmem_vunmap(to_drm_gem_shmem_obj(obj), map);
}

static inline int
drm_gem_shmem_object_mmap(struct drm_gem_object *obj,
    struct vm_area_struct *vma)
{

	return drm_gem_shmem_mmap(to_drm_gem_shmem_obj(obj), vma);
}

#endif	/* _DRM618_DRM_GEM_SHMEM_HELPER_H_ */
