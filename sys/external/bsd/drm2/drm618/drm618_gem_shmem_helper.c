/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: virtgpu_object.c, virtgpu_plane.c,
 * virtgpu_drv.h of dist618 (MIT), drm_gem.h, drm_prime.h, drm_drv.h of
 * dist618 (MIT), drm618/include/drm/drm_gem_shmem_helper.h, efidrm618.c (the
 * GEM object over a shmem file of this tree); the GPL original was not read.
 *
 * GEM objects over a shmem file, see drm_gem_shmem_helper.h.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_prime.h>

#include <drm618_page.h>

/* ---- the pages ---------------------------------------------------------- */

/* One more holder of the pages; the pages are taken from the file for the first. */
static int
shmem_get_pages(struct drm_gem_shmem_object *shmem)
{
	struct page **pages;

	mutex_lock(&shmem->pages_lock);
	if (shmem->pages_use_count == 0) {
		pages = drm_gem_get_pages(&shmem->base);
		if (IS_ERR(pages)) {
			mutex_unlock(&shmem->pages_lock);
			return PTR_ERR(pages);
		}
		shmem->pages = pages;
	}
	shmem->pages_use_count++;
	mutex_unlock(&shmem->pages_lock);
	return 0;
}

static void
shmem_put_pages(struct drm_gem_shmem_object *shmem)
{

	mutex_lock(&shmem->pages_lock);
	KASSERT(shmem->pages_use_count > 0);
	if (--shmem->pages_use_count == 0) {
		drm_gem_put_pages(&shmem->base, shmem->pages, true, true);
		shmem->pages = NULL;
	}
	mutex_unlock(&shmem->pages_lock);
}

/*
 * The largest object of a device whose driver cannot handle more (the ring of
 * the virtio-gpu takes the list of backing pages of an object of this size in
 * one command): set by the driver's layer, 0 clears it.  The table is small:
 * few devices have a limit.
 */
void	drm618_gem_set_limit(device_t, size_t);

static struct {
	device_t	dev;
	size_t		bytes;
} shmem_limits[4];
static kmutex_t shmem_limits_lock;
static bool shmem_limits_init;

void
drm618_gem_set_limit(device_t dev, size_t bytes)
{
	unsigned int i;

	if (!shmem_limits_init) {
		mutex_init(&shmem_limits_lock, MUTEX_DEFAULT, IPL_NONE);
		shmem_limits_init = true;
	}
	mutex_enter(&shmem_limits_lock);
	/* The slot of this device first, wherever it is, then a free one.  */
	for (i = 0; i < __arraycount(shmem_limits); i++)
		if (dev != NULL && shmem_limits[i].dev == dev)
			break;
	if (i == __arraycount(shmem_limits) && bytes != 0) {
		for (i = 0; i < __arraycount(shmem_limits); i++)
			if (shmem_limits[i].dev == NULL)
				break;
	}
	if (i < __arraycount(shmem_limits)) {
		shmem_limits[i].dev = bytes != 0 ? dev : NULL;
		shmem_limits[i].bytes = bytes;
	} else if (bytes != 0) {
		printf("gem shmem: no room for the limit of a device\n");
	}
	mutex_exit(&shmem_limits_lock);
}

static size_t
shmem_limit(device_t dev)
{
	size_t bytes = 0;
	unsigned int i;

	if (!shmem_limits_init)
		return 0;
	mutex_enter(&shmem_limits_lock);
	for (i = 0; i < __arraycount(shmem_limits); i++)
		if (shmem_limits[i].dev == dev && dev != NULL)
			bytes = shmem_limits[i].bytes;
	mutex_exit(&shmem_limits_lock);
	return bytes;
}

/* ---- the object ---------------------------------------------------------- */

static const struct drm_gem_object_funcs drm_gem_shmem_funcs;

struct drm_gem_shmem_object *
drm_gem_shmem_create(struct drm_device *dev, size_t size)
{
	struct drm_gem_shmem_object *shmem;
	struct drm_gem_object *obj;
	size_t limit;
	int ret;

	size = PAGE_ALIGN(size);
	if (size == 0 || size > (size_t)UINT_MAX << PAGE_SHIFT)
		return ERR_PTR(-EINVAL);
	limit = shmem_limit(dev->dev);
	if (limit != 0 && size > limit) {
		DRM_DEV_ERROR(dev->dev, "gem shmem: an object of %zu bytes is"
		    " refused, the limit of the device is %zu\n", size, limit);
		return ERR_PTR(-EFBIG);
	}
	if (dev->driver->gem_create_object) {
		obj = dev->driver->gem_create_object(dev, size);
		if (IS_ERR(obj))
			return ERR_CAST(obj);
	} else {
		shmem = kzalloc(sizeof(*shmem), GFP_KERNEL);
		if (shmem == NULL)
			return ERR_PTR(-ENOMEM);
		obj = &shmem->base;
	}
	shmem = to_drm_gem_shmem_obj(obj);
	if (obj->funcs == NULL)
		obj->funcs = &drm_gem_shmem_funcs;
	mutex_init(&shmem->pages_lock);
	ret = drm_gem_object_init(dev, obj, size);
	if (ret) {
		DRM_DEV_ERROR(dev->dev, "gem shmem: the file of %zu bytes: %d\n",
		    size, ret);
		mutex_destroy(&shmem->pages_lock);
		kfree(obj);
		return ERR_PTR(ret);
	}
	return shmem;
}

/* The object is no longer used: give everything back and free it. */
void
drm_gem_shmem_free(struct drm_gem_shmem_object *shmem)
{
	struct drm_gem_object *obj = &shmem->base;

	if (shmem->sgt != NULL) {
		sg_free_table(shmem->sgt);
		kfree(shmem->sgt);
		shmem->sgt = NULL;
		shmem_put_pages(shmem);
	}
	if (shmem->vaddr != NULL) {
		vunmap(shmem->vaddr);
		shmem->vaddr = NULL;
		shmem->vmap_use_count = 0;
		shmem_put_pages(shmem);
	}
	if (shmem->pages_use_count != 0)
		DRM_DEV_ERROR(obj->dev->dev, "gem shmem: freed with %u uses of"
		    " the pages\n", shmem->pages_use_count);
	drm_gem_object_release(obj);
	mutex_destroy(&shmem->pages_lock);
	kfree(shmem);
}

int
drm_gem_shmem_pin(struct drm_gem_shmem_object *shmem)
{

	return shmem_get_pages(shmem);
}

void
drm_gem_shmem_unpin(struct drm_gem_shmem_object *shmem)
{

	shmem_put_pages(shmem);
}

int
drm_gem_shmem_vmap(struct drm_gem_shmem_object *shmem, struct iosys_map *map)
{
	void *va;
	int ret;

	mutex_lock(&shmem->pages_lock);
	if (shmem->vaddr != NULL) {		/* the mapping holds the pages */
		shmem->vmap_use_count++;
		iosys_map_set_vaddr(map, shmem->vaddr);
		mutex_unlock(&shmem->pages_lock);
		return 0;
	}
	mutex_unlock(&shmem->pages_lock);

	ret = shmem_get_pages(shmem);		/* the use of the mapping */
	if (ret)
		return ret;
	mutex_lock(&shmem->pages_lock);
	if (shmem->vaddr != NULL) {		/* mapped meanwhile by another */
		shmem->vmap_use_count++;
		iosys_map_set_vaddr(map, shmem->vaddr);
		mutex_unlock(&shmem->pages_lock);
		shmem_put_pages(shmem);
		return 0;
	}
	va = vmap(shmem->pages, shmem->base.size >> PAGE_SHIFT, 0, PAGE_KERNEL);
	if (va == NULL) {
		mutex_unlock(&shmem->pages_lock);
		shmem_put_pages(shmem);
		DRM_DEV_ERROR(shmem->base.dev->dev,
		    "gem shmem: no kernel address range for %zu bytes\n",
		    shmem->base.size);
		return -ENOMEM;
	}
	shmem->vaddr = va;
	shmem->vmap_use_count = 1;
	iosys_map_set_vaddr(map, va);
	mutex_unlock(&shmem->pages_lock);
	return 0;
}

void
drm_gem_shmem_vunmap(struct drm_gem_shmem_object *shmem, struct iosys_map *map)
{
	void *va = NULL;

	mutex_lock(&shmem->pages_lock);
	if (shmem->vmap_use_count == 0) {
		mutex_unlock(&shmem->pages_lock);
		DRM_DEV_ERROR(shmem->base.dev->dev,
		    "gem shmem: vunmap without vmap\n");
		return;
	}
	if (--shmem->vmap_use_count == 0) {
		va = shmem->vaddr;
		shmem->vaddr = NULL;
	}
	mutex_unlock(&shmem->pages_lock);
	if (va != NULL) {
		vunmap(va);
		shmem_put_pages(shmem);
	}
	iosys_map_clear(map);
}

/* ---- tables --------------------------------------------------------------- */

struct sg_table *
drm_gem_shmem_get_sg_table(struct drm_gem_shmem_object *shmem)
{
	struct sg_table *sgt;

	/* the caller holds a pin */
	mutex_lock(&shmem->pages_lock);
	if (shmem->pages == NULL) {
		mutex_unlock(&shmem->pages_lock);
		return ERR_PTR(-EINVAL);
	}
	sgt = drm_prime_pages_to_sg(shmem->base.dev, shmem->pages,
	    shmem->base.size >> PAGE_SHIFT);
	mutex_unlock(&shmem->pages_lock);
	return sgt;
}

struct sg_table *
drm_gem_shmem_get_pages_sgt(struct drm_gem_shmem_object *shmem)
{
	struct sg_table *sgt;
	int ret;

	mutex_lock(&shmem->pages_lock);
	sgt = shmem->sgt;
	mutex_unlock(&shmem->pages_lock);
	if (sgt != NULL)
		return sgt;
	ret = shmem_get_pages(shmem);		/* kept until the object dies */
	if (ret)
		return ERR_PTR(ret);
	sgt = drm_gem_shmem_get_sg_table(shmem);
	if (IS_ERR(sgt)) {
		shmem_put_pages(shmem);
		return sgt;
	}
	/* The host of a table here sees physical memory: no IOMMU. */
	drm618_sgt_dma_physical(sgt);
	mutex_lock(&shmem->pages_lock);
	if (shmem->sgt != NULL) {		/* made meanwhile by another */
		struct sg_table *old = shmem->sgt;

		mutex_unlock(&shmem->pages_lock);
		sg_free_table(sgt);
		kfree(sgt);
		shmem_put_pages(shmem);
		return old;
	}
	shmem->sgt = sgt;
	mutex_unlock(&shmem->pages_lock);
	return sgt;
}

void
drm_gem_shmem_print_info(const struct drm_gem_shmem_object *shmem,
    struct drm_printer *p, unsigned int indent)
{

	drm_printf_indent(p, indent, "pages_use_count=%u\n",
	    shmem->pages_use_count);
	drm_printf_indent(p, indent, "vmap_use_count=%u\n",
	    shmem->vmap_use_count);
	drm_printf_indent(p, indent, "vaddr=%p\n", shmem->vaddr);
}

/* ---- the user mapping -------------------------------------------------------- */

static vm_fault_t
shmem_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct drm_gem_shmem_object *shmem =
	    to_drm_gem_shmem_obj((struct drm_gem_object *)vma->vm_private_data);
	pgoff_t page_offset = (vmf->address - vma->vm_start) >> PAGE_SHIFT;
	struct page *page = NULL;

	mutex_lock(&shmem->pages_lock);
	if (shmem->pages != NULL &&
	    page_offset < (shmem->base.size >> PAGE_SHIFT))
		page = shmem->pages[page_offset];
	mutex_unlock(&shmem->pages_lock);
	if (page == NULL)
		return VM_FAULT_SIGBUS;
	return vmf_insert_page(vma, vmf->address, page);
}

/* Another reference of the mapping (fork, split): one more use, one more object reference. */
static void
shmem_vm_open(struct vm_area_struct *vma)
{
	struct drm_gem_shmem_object *shmem =
	    to_drm_gem_shmem_obj((struct drm_gem_object *)vma->vm_private_data);

	drm_gem_vm_open(vma);
	mutex_lock(&shmem->pages_lock);
	KASSERT(shmem->pages_use_count > 0);
	shmem->pages_use_count++;
	mutex_unlock(&shmem->pages_lock);
}

static void
shmem_vm_close(struct vm_area_struct *vma)
{
	struct drm_gem_shmem_object *shmem =
	    to_drm_gem_shmem_obj((struct drm_gem_object *)vma->vm_private_data);

	shmem_put_pages(shmem);
	drm_gem_vm_close(vma);
}

const struct vm_operations_struct drm_gem_shmem_vm_ops = {
	.fault	= shmem_fault,
	.open	= shmem_vm_open,
	.close	= shmem_vm_close,
};

int
drm_gem_shmem_mmap(struct drm_gem_shmem_object *shmem,
    struct vm_area_struct *vma)
{
	int ret;

	ret = shmem_get_pages(shmem);		/* the use of the mapping */
	if (ret)
		return ret;
	vm_flags_set(vma, VM_DONTEXPAND | VM_DONTDUMP | VM_IO | VM_PFNMAP);
	/* ordinary cached memory: no write combining for pages of the page cache */
	vma->vm_page_prot = vm_get_page_prot(vma->vm_flags);
	return 0;
}

static const struct drm_gem_object_funcs drm_gem_shmem_funcs = {
	.free		= drm_gem_shmem_object_free,
	.print_info	= drm_gem_shmem_object_print_info,
	.pin		= drm_gem_shmem_object_pin,
	.unpin		= drm_gem_shmem_object_unpin,
	.get_sg_table	= drm_gem_shmem_object_get_sg_table,
	.vmap		= drm_gem_shmem_object_vmap,
	.vunmap		= drm_gem_shmem_object_vunmap,
	.mmap		= drm_gem_shmem_object_mmap,
	.vm_ops		= &drm_gem_shmem_vm_ops,
};
