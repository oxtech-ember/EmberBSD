/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD native DMA/GEM buffer ownership and UVM mapping.
 * UAPI semantics: MIT Linux PowerVR 7b63ef2d55f24519e7e9e5f4d15dbea03f126e40.
 * The existing native CMA pager supplies CPU faults and mapping references.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/file.h>
#include <sys/kmem.h>
#include <sys/mman.h>
#include <sys/systm.h>
#include <linux/err.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_vma_manager.h>
#include <arm/sunxi/sun60i_a733_gpu_mmu.h>
#include "pvr_drm.h"
#include "pvr_drv.h"

#define PVR_DEVICE_PAGE_SIZE	4096

struct pvr_bo {
	/* The native CMA pager reaches this member from the GEM object. */
	struct drm_gem_cma_object cma;
	uint64_t flags;
	int dma_owned;
	bool firmware_pinned;
};

static struct pvr_bo *
pvr_to_bo(struct drm_gem_object *obj)
{

	return container_of(to_drm_gem_cma_obj(obj), struct pvr_bo, cma);
}

static void
pvr_bo_free(struct drm_gem_object *obj)
{
	struct drm_device *dev = obj->dev;
	struct pvr_bo *bo = pvr_to_bo(obj);
	struct drm_gem_cma_object *cma = &bo->cma;

	KASSERT(!bo->firmware_pinned);
	drm_gem_object_release(obj);
	if (bo->dma_owned != 0)
		bus_dmamap_sync(cma->dmat, cma->dmamap, 0, cma->dmasize,
		    ((bo->dma_owned & BUS_DMASYNC_PREWRITE) != 0 ?
		    BUS_DMASYNC_POSTWRITE : 0) |
		    ((bo->dma_owned & BUS_DMASYNC_PREREAD) != 0 ?
		    BUS_DMASYNC_POSTREAD : 0));
	bus_dmamap_unload(cma->dmat, cma->dmamap);
	bus_dmamap_destroy(cma->dmat, cma->dmamap);
	bus_dmamem_unmap(cma->dmat, cma->vaddr, cma->dmasize);
	bus_dmamem_free(cma->dmat, cma->dmasegs, 1);
	kmem_free(bo, sizeof(*bo));
	/* CPU mappings can outlive both their GEM handle and their DRM fd. */
	drm_dev_put(dev);
}

static const struct drm_gem_object_funcs pvr_bo_funcs = {
	.free = pvr_bo_free,
};

int
pvr_bo_mapping(struct drm_gem_object *obj, uint64_t offset, uint64_t size,
    uint64_t *address, uint64_t *flags)
{
	struct pvr_bo *bo;
	bus_dmamap_t map;
	uint64_t pa, options = 0;
	const uint64_t limit = UINT64_C(1) << A733_GPU_MMU_PA_BITS;

	if (obj == NULL || obj->funcs != &pvr_bo_funcs ||
	    address == NULL || flags == NULL || size == 0 ||
	    ((offset | size) & (PAGE_SIZE - 1)) != 0 ||
	    offset > obj->size || size > obj->size - offset)
		return EINVAL;
	bo = pvr_to_bo(obj);
	map = bo->cma.dmamap;
	/* A bounce buffer cannot preserve CPU/GPU identity across future jobs. */
	if ((map->_dm_flags & _BUS_DMAMAP_IS_BOUNCING) != 0 ||
	    map->dm_nsegs != 1 || map->dm_mapsize < obj->size ||
	    map->dm_segs[0].ds_len < obj->size)
		return EOPNOTSUPP;
	pa = map->dm_segs[0].ds_addr;
	if ((pa & (A733_GPU_MMU_PAGE_SIZE - 1)) != 0 ||
	    pa >= limit || offset >= limit - pa ||
	    size > limit - pa - offset)
		return EINVAL;
	if ((bo->flags & DRM_PVR_BO_BYPASS_DEVICE_CACHE) != 0)
		options |= A733_GPU_MMU_PTE_SLC_BYPASS;
	if ((bo->flags & DRM_PVR_BO_PM_FW_PROTECT) != 0)
		options |= A733_GPU_MMU_PTE_PM_FW_PROTECT;
	*address = pa + offset;
	*flags = options;
	return 0;
}

static struct pvr_bo *
pvr_bo_alloc(struct drm_device *dev, size_t size, uint64_t flags)
{
	struct pvr_bo *bo;
	struct drm_gem_cma_object *cma;
	int error, nsegs;

	bo = kmem_zalloc(sizeof(*bo), KM_SLEEP);
	cma = &bo->cma;
	cma->dmat = dev->dmat;
	cma->dmasize = size;
	bo->flags = flags;
	error = bus_dmamem_alloc(cma->dmat, size, PAGE_SIZE, 0,
	    cma->dmasegs, 1, &nsegs, BUS_DMA_NOWAIT);
	if (error != 0)
		goto free_bo;
	/* Use the same CPU memory attributes as the existing CMA UVM pager. */
	error = bus_dmamem_map(cma->dmat, cma->dmasegs, nsegs, size,
	    &cma->vaddr, BUS_DMA_NOWAIT | BUS_DMA_PREFETCHABLE);
	if (error != 0)
		goto free_dma;
	error = bus_dmamap_create(cma->dmat, size, 1, size, 0,
	    BUS_DMA_NOWAIT, &cma->dmamap);
	if (error != 0)
		goto unmap;
	error = bus_dmamap_load(cma->dmat, cma->dmamap, cma->vaddr,
	    size, NULL, BUS_DMA_NOWAIT);
	if (error != 0)
		goto destroy;
	memset(cma->vaddr, 0, size);
	bus_dmamap_sync(cma->dmat, cma->dmamap, 0, size,
	    BUS_DMASYNC_PREWRITE);
	bo->dma_owned = BUS_DMASYNC_PREWRITE;
	drm_dev_get(dev);
	drm_gem_private_object_init(dev, &cma->base, size);
	cma->base.funcs = &pvr_bo_funcs;
	return bo;

destroy:
	bus_dmamap_destroy(cma->dmat, cma->dmamap);
unmap:
	bus_dmamem_unmap(cma->dmat, cma->vaddr, size);
free_dma:
	bus_dmamem_free(cma->dmat, cma->dmasegs, nsegs);
free_bo:
	kmem_free(bo, sizeof(*bo));
	return ERR_PTR(-error);
}

int
pvr_create_bo(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_pvr_ioctl_create_bo_args *args = data;
	struct pvr_bo *bo;
	const uint64_t conflict = DRM_PVR_BO_PM_FW_PROTECT |
	    DRM_PVR_BO_ALLOW_CPU_USERSPACE_ACCESS;
	size_t size;
	uint32_t handle;
	int error;

	if (args->_padding_c != 0 || args->size == 0 ||
	    args->size > SIZE_MAX - (PAGE_SIZE - 1) ||
	    (args->size & (PVR_DEVICE_PAGE_SIZE - 1)) != 0 ||
	    (args->flags & ~DRM_PVR_BO_FLAGS_MASK) != 0 ||
	    (args->flags & conflict) == conflict)
		return -EINVAL;
	size = roundup2((size_t)args->size, PAGE_SIZE);
	/* Native uvm_pglistalloc() represents the page count as an int. */
	if (size / PAGE_SIZE > INT_MAX)
		return -EINVAL;
	bo = pvr_bo_alloc(dev, size, args->flags);
	if (IS_ERR(bo))
		return PTR_ERR(bo);
	error = drm_gem_handle_create(file, &bo->cma.base, &handle);
	if (error == 0)
		args->handle = handle;
	/* Keep the output unchanged on failure; drop the constructor reference. */
	drm_gem_object_put_unlocked(&bo->cma.base);
	return error;
}

int
pvr_get_bo_mmap_offset(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_pvr_ioctl_get_bo_mmap_offset_args *args = data;
	struct drm_gem_object *obj;
	int error;

	if (args->_padding_4 != 0)
		return -EINVAL;
	obj = drm_gem_object_lookup(file, args->handle);
	if (obj == NULL)
		return -ENOENT;
	error = drm_gem_create_mmap_offset(obj);
	if (error == 0)
		args->offset = drm_vma_node_offset_addr(&obj->vma_node);
	drm_gem_object_put_unlocked(obj);
	return error;
}

int
pvr_mmap_object(struct drm_device *dev, off_t byte_offset, size_t size,
    int prot, struct uvm_object **uobjp, voff_t *offsetp, struct file *fp)
{
	struct drm_vma_offset_node *node;
	struct drm_gem_object *obj = NULL;
	unsigned long start;
	int error;

	if (byte_offset < 0 || (byte_offset & (PAGE_SIZE - 1)) != 0 ||
	    size == 0 || (size & (PAGE_SIZE - 1)) != 0 ||
	    (prot & ~(PROT_READ | PROT_WRITE)) != 0 ||
	    ((uint64_t)byte_offset >> PAGE_SHIFT) > ULONG_MAX)
		return -EINVAL;
	start = (unsigned long)((uint64_t)byte_offset >> PAGE_SHIFT);
	drm_vma_offset_lock_lookup(dev->vma_offset_manager);
	node = drm_vma_offset_lookup_locked(dev->vma_offset_manager,
	    start, size >> PAGE_SHIFT);
	/* Linux permits a shorter mapping, but requires the object's start. */
	if (node != NULL && drm_vma_node_start(node) == start) {
		obj = container_of(node, struct drm_gem_object, vma_node);
		/* Never revive an object whose final release is waiting for this lock. */
		if (!kref_get_unless_zero(&obj->refcount))
			obj = NULL;
	}
	drm_vma_offset_unlock_lookup(dev->vma_offset_manager);
	if (obj == NULL)
		return -EINVAL;
	if (!drm_vma_node_is_allowed(node, fp->f_data))
		error = -EACCES;
	else if ((pvr_to_bo(obj)->flags & DRM_PVR_BO_ALLOW_CPU_USERSPACE_ACCESS) == 0)
		error = -EINVAL;
	else {
		*uobjp = &obj->gemo_uvmobj;
		*offsetp = 0;
		/* The CMA pager owns the live reference until the mapping is detached. */
		return 0;
	}
	drm_gem_object_put_unlocked(obj);
	return error;
}

int
pvr_bo_cpu(struct drm_gem_object *obj, uint64_t offset, size_t size,
    uint64_t flags, void **cpu)
{
	struct pvr_bo *bo;

	if (obj == NULL || obj->funcs != &pvr_bo_funcs || cpu == NULL ||
	    size == 0 || offset > obj->size || size > obj->size - offset)
		return EINVAL;
	bo = pvr_to_bo(obj);
	if ((bo->flags & flags) != flags)
		return EACCES;
	*cpu = (uint8_t *)bo->cma.vaddr + offset;
	return 0;
}

void
pvr_bo_sync(struct drm_gem_object *obj, int operations)
{
	struct pvr_bo *bo = pvr_to_bo(obj);
	int active = operations;

	KASSERT(obj->funcs == &pvr_bo_funcs);
	/* A BO can have several VM mappings. Close each DMA direction once. */
	if ((bo->dma_owned & BUS_DMASYNC_PREWRITE) == 0)
		active &= ~BUS_DMASYNC_POSTWRITE;
	if ((bo->dma_owned & BUS_DMASYNC_PREREAD) == 0)
		active &= ~BUS_DMASYNC_POSTREAD;
	if (active == 0)
		return;
	bus_dmamap_sync(bo->cma.dmat, bo->cma.dmamap, 0, bo->cma.dmasize,
	    active);
	if ((active & BUS_DMASYNC_POSTWRITE) != 0)
		bo->dma_owned &= ~BUS_DMASYNC_PREWRITE;
	if ((active & BUS_DMASYNC_POSTREAD) != 0)
		bo->dma_owned &= ~BUS_DMASYNC_PREREAD;
	bo->dma_owned |= active & (BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
}

int
pvr_bo_pin(struct drm_gem_object *obj)
{
	struct pvr_bo *bo;

	if (obj == NULL || obj->funcs != &pvr_bo_funcs)
		return EINVAL;
	bo = pvr_to_bo(obj);
	if (bo->firmware_pinned)
		return EBUSY;
	bo->firmware_pinned = true;
	return 0;
}

void
pvr_bo_unpin(struct drm_gem_object *obj)
{
	struct pvr_bo *bo = pvr_to_bo(obj);

	KASSERT(obj->funcs == &pvr_bo_funcs && bo->firmware_pinned);
	bo->firmware_pinned = false;
}
