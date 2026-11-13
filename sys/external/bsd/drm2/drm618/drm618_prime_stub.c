/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD.  Signatures from the MIT
 * header drm_prime.h and the declarations of drm_prime.c.
 *
 * Stand-in for dist618/drm/drm_prime.c (dma-buf import and export), which needs
 * the 6.18 dma-buf (struct dma_buf with its file, the 6.18 dma_buf_ops,
 * scatter/gather table mapping): not built yet (drm618/tools/not-built.txt).
 * Every function says so on the console, once, and fails with -ENODEV (the
 * functions that return an object, with ERR_PTR(-ENODEV)); never silently.
 * The file private part (drm_prime_init_file_private and friends) does
 * nothing, as there are no buffers to remember.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/atomic.h>
#include <sys/systm.h>

#include <linux/errno.h>
#include <linux/err.h>

#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/drm_prime.h>

#include "../dist618/drm/drm_internal.h"

static volatile unsigned drm618_prime_said;

static void
drm618_prime_unavailable(const char *fn)
{

	if (atomic_swap_uint(&drm618_prime_said, 1) == 0)
		printf("drm618: dma-buf (PRIME) is not available yet: %s and the"
		    " other drm_prime functions fail with ENODEV\n", fn);
}

int
drm_prime_add_buf_handle(struct drm_prime_file_private *prime_fpriv, struct dma_buf *dma_buf, uint32_t handle)
{
	(void)prime_fpriv;
	(void)dma_buf;
	(void)handle;
	drm618_prime_unavailable(__func__);
	return -ENODEV;
}

void
drm_prime_remove_buf_handle(struct drm_prime_file_private *prime_fpriv, uint32_t handle)
{
	(void)prime_fpriv;
	(void)handle;
	drm618_prime_unavailable(__func__);
}

void
drm_prime_init_file_private(struct drm_prime_file_private *prime_fpriv)
{
	(void)prime_fpriv;
	drm618_prime_unavailable(__func__);
}

void
drm_prime_destroy_file_private(struct drm_prime_file_private *prime_fpriv)
{
	(void)prime_fpriv;
	drm618_prime_unavailable(__func__);
}

struct dma_buf *
drm_gem_dmabuf_export(struct drm_device *dev, struct dma_buf_export_info *exp_info)
{
	(void)dev;
	(void)exp_info;
	drm618_prime_unavailable(__func__);
	return ERR_PTR(-ENODEV);
}

void
drm_gem_dmabuf_release(struct dma_buf *dma_buf)
{
	(void)dma_buf;
	drm618_prime_unavailable(__func__);
}

int
drm_gem_prime_fd_to_handle(struct drm_device *dev, struct drm_file *file_priv, int prime_fd, uint32_t *handle)
{
	(void)dev;
	(void)file_priv;
	(void)prime_fd;
	(void)handle;
	drm618_prime_unavailable(__func__);
	return -ENODEV;
}

int
drm_prime_fd_to_handle_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	(void)dev;
	(void)data;
	(void)file_priv;
	drm618_prime_unavailable(__func__);
	return -ENODEV;
}

struct dma_buf *
drm_gem_prime_handle_to_dmabuf(struct drm_device *dev, struct drm_file *file_priv, uint32_t handle, uint32_t flags)
{
	(void)dev;
	(void)file_priv;
	(void)handle;
	(void)flags;
	drm618_prime_unavailable(__func__);
	return ERR_PTR(-ENODEV);
}

int
drm_gem_prime_handle_to_fd(struct drm_device *dev, struct drm_file *file_priv, uint32_t handle, uint32_t flags, int *prime_fd)
{
	(void)dev;
	(void)file_priv;
	(void)handle;
	(void)flags;
	(void)prime_fd;
	drm618_prime_unavailable(__func__);
	return -ENODEV;
}

int
drm_prime_handle_to_fd_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	(void)dev;
	(void)data;
	(void)file_priv;
	drm618_prime_unavailable(__func__);
	return -ENODEV;
}

int
drm_gem_map_attach(struct dma_buf *dma_buf, struct dma_buf_attachment *attach)
{
	(void)dma_buf;
	(void)attach;
	drm618_prime_unavailable(__func__);
	return -ENODEV;
}

void
drm_gem_map_detach(struct dma_buf *dma_buf, struct dma_buf_attachment *attach)
{
	(void)dma_buf;
	(void)attach;
	drm618_prime_unavailable(__func__);
}

struct sg_table *
drm_gem_map_dma_buf(struct dma_buf_attachment *attach, enum dma_data_direction dir)
{
	(void)attach;
	(void)dir;
	drm618_prime_unavailable(__func__);
	return ERR_PTR(-ENODEV);
}

void
drm_gem_unmap_dma_buf(struct dma_buf_attachment *attach, struct sg_table *sgt, enum dma_data_direction dir)
{
	(void)attach;
	(void)sgt;
	(void)dir;
	drm618_prime_unavailable(__func__);
}

int
drm_gem_dmabuf_vmap(struct dma_buf *dma_buf, struct iosys_map *map)
{
	(void)dma_buf;
	(void)map;
	drm618_prime_unavailable(__func__);
	return -ENODEV;
}

void
drm_gem_dmabuf_vunmap(struct dma_buf *dma_buf, struct iosys_map *map)
{
	(void)dma_buf;
	(void)map;
	drm618_prime_unavailable(__func__);
}

int
drm_gem_prime_mmap(struct drm_gem_object *obj, struct vm_area_struct *vma)
{
	(void)obj;
	(void)vma;
	drm618_prime_unavailable(__func__);
	return -ENODEV;
}

int
drm_gem_dmabuf_mmap(struct dma_buf *dma_buf, struct vm_area_struct *vma)
{
	(void)dma_buf;
	(void)vma;
	drm618_prime_unavailable(__func__);
	return -ENODEV;
}

struct sg_table *
drm_prime_pages_to_sg(struct drm_device *dev, struct page **pages, unsigned int nr_pages)
{
	(void)dev;
	(void)pages;
	(void)nr_pages;
	drm618_prime_unavailable(__func__);
	return ERR_PTR(-ENODEV);
}

unsigned long
drm_prime_get_contiguous_size(struct sg_table *sgt)
{
	(void)sgt;
	drm618_prime_unavailable(__func__);
	return 0;
}

struct dma_buf *
drm_gem_prime_export(struct drm_gem_object *obj, int flags)
{
	(void)obj;
	(void)flags;
	drm618_prime_unavailable(__func__);
	return ERR_PTR(-ENODEV);
}

bool
drm_gem_is_prime_exported_dma_buf(struct drm_device *dev, struct dma_buf *dma_buf)
{
	(void)dev;
	(void)dma_buf;
	drm618_prime_unavailable(__func__);
	return false;
}

struct drm_gem_object *
drm_gem_prime_import_dev(struct drm_device *dev, struct dma_buf *dma_buf, struct device *attach_dev)
{
	(void)dev;
	(void)dma_buf;
	(void)attach_dev;
	drm618_prime_unavailable(__func__);
	return ERR_PTR(-ENODEV);
}

struct drm_gem_object *
drm_gem_prime_import(struct drm_device *dev, struct dma_buf *dma_buf)
{
	(void)dev;
	(void)dma_buf;
	drm618_prime_unavailable(__func__);
	return ERR_PTR(-ENODEV);
}

int __deprecated
drm_prime_sg_to_page_array(struct sg_table *sgt, struct page **pages, int max_entries)
{
	(void)sgt;
	(void)pages;
	(void)max_entries;
	drm618_prime_unavailable(__func__);
	return -ENODEV;
}

int
drm_prime_sg_to_dma_addr_array(struct sg_table *sgt, dma_addr_t *addrs, int max_entries)
{
	(void)sgt;
	(void)addrs;
	(void)max_entries;
	drm618_prime_unavailable(__func__);
	return -ENODEV;
}

void
drm_prime_gem_destroy(struct drm_gem_object *obj, struct sg_table *sg)
{
	(void)obj;
	(void)sg;
	drm618_prime_unavailable(__func__);
}
