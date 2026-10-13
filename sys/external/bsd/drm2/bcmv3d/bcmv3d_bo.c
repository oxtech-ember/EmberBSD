/* Origin: EmberBSD native BCM2712 GEM buffer ownership, 2026-10-11. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/kmem.h>
#include <sys/systm.h>
#include <linux/err.h>
#include <drm/drm_gem.h>
#include <drm/drm_vma_manager.h>
#include "bcmv3d_drv.h"

static void
bcmv3d_bo_free(struct drm_gem_object *obj)
{
	struct bcmv3d_device *sc = obj->dev->dev_private;
	struct bcmv3d_bo *bo = bcmv3d_to_bo(obj);
	struct drm_gem_cma_object *cma = &bo->cma;

	/* A submitted job holds its own reference, including poisoned jobs. */
	KASSERT(bo->busy == 0);
	drm_gem_free_mmap_offset(obj);
	drm_gem_object_release(obj);
	bus_dmamap_unload(cma->dmat, cma->dmamap);
	bus_dmamap_destroy(cma->dmat, cma->dmamap);
	bus_dmamem_unmap(cma->dmat, cma->vaddr, cma->dmasize);
	bus_dmamem_free(cma->dmat, cma->dmasegs, 1);
	mutex_enter(&sc->memory_lock);
	vmem_free(sc->va, bo->gpu, cma->dmasize);
	KASSERT(sc->allocated >= cma->dmasize);
	sc->allocated -= cma->dmasize;
	mutex_exit(&sc->memory_lock);
	kmem_free(bo, sizeof(*bo));
}

static const struct drm_gem_object_funcs bcmv3d_bo_funcs = {
	.free = bcmv3d_bo_free,
};

struct bcmv3d_bo *
bcmv3d_bo_alloc(struct bcmv3d_device *sc, uint32_t requested)
{
	struct bcmv3d_bo *bo;
	struct drm_gem_cma_object *cma;
	bus_addr_t address;
	size_t size;
	int error, nsegs;

	if (requested == 0 || requested > BCMV3D_BO_MAX)
		return ERR_PTR(-EINVAL);
	size = ((size_t)requested + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
	bo = kmem_zalloc(sizeof(*bo), KM_SLEEP);
	cma = &bo->cma;
	cma->dmat = sc->dmat;
	cma->dmasize = size;
	mutex_enter(&sc->memory_lock);
	if (size > BCMV3D_BO_TOTAL - sc->allocated) {
		mutex_exit(&sc->memory_lock);
		error = ENOMEM;
		goto free_bo;
	}
	error = vmem_alloc(sc->va, size, VM_BESTFIT | VM_NOSLEEP, &bo->gpu);
	if (error != 0) {
		mutex_exit(&sc->memory_lock);
		goto free_bo;
	}
	sc->allocated += size;
	mutex_exit(&sc->memory_lock);

	error = bus_dmamem_alloc(sc->dmat, size, PAGE_SIZE, 0,
	    cma->dmasegs, 1, &nsegs, BUS_DMA_WAITOK);
	if (error != 0)
		goto release_va;
	/* Match the existing CMA pager's write-combining userspace mapping. */
	error = bus_dmamem_map(sc->dmat, cma->dmasegs, nsegs, size,
	    &cma->vaddr, BUS_DMA_WAITOK | BUS_DMA_PREFETCHABLE);
	if (error != 0)
		goto free_dma;
	error = bus_dmamap_create(sc->dmat, size, 1, size, 0,
	    BUS_DMA_WAITOK, &cma->dmamap);
	if (error != 0)
		goto unmap;
	error = bus_dmamap_load(sc->dmat, cma->dmamap, cma->vaddr,
	    size, NULL, BUS_DMA_WAITOK);
	if (error != 0)
		goto destroy;
	address = cma->dmamap->dm_segs[0].ds_addr;
	if (cma->dmamap->dm_nsegs != 1 ||
	    cma->dmamap->dm_segs[0].ds_len != size ||
	    (address & (PAGE_SIZE - 1)) != 0 ||
	    address > (UINT64_C(1) << 36) - size) {
		error = EINVAL;
		goto unload;
	}
	memset(cma->vaddr, 0, size);
	drm_gem_private_object_init(sc->drm, &cma->base, size);
	cma->base.funcs = &bcmv3d_bo_funcs;
	return bo;

unload:
	bus_dmamap_unload(sc->dmat, cma->dmamap);
destroy:
	bus_dmamap_destroy(sc->dmat, cma->dmamap);
unmap:
	bus_dmamem_unmap(sc->dmat, cma->vaddr, size);
free_dma:
	bus_dmamem_free(sc->dmat, cma->dmasegs, nsegs);
release_va:
	mutex_enter(&sc->memory_lock);
	vmem_free(sc->va, bo->gpu, size);
	sc->allocated -= size;
	mutex_exit(&sc->memory_lock);
free_bo:
	kmem_free(bo, sizeof(*bo));
	return ERR_PTR(-error);
}

int
bcmv3d_create_bo(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_v3d_create_bo *args = data;
	struct bcmv3d_bo *bo;
	uint32_t handle;
	int error;

	args->handle = 0;
	args->offset = 0;
	if (args->flags != 0)
		return -EINVAL;
	bo = bcmv3d_bo_alloc(dev->dev_private, args->size);
	if (IS_ERR(bo))
		return PTR_ERR(bo);
	error = drm_gem_handle_create(file, &bo->cma.base, &handle);
	if (error == 0) {
		args->handle = handle;
		args->offset = bo->gpu;
	}
	/* The constructor reference is released exactly once on either path. */
	drm_gem_object_put_unlocked(&bo->cma.base);
	return error;
}

int
bcmv3d_mmap_bo(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_v3d_mmap_bo *args = data;
	struct drm_gem_object *obj;
	int error;

	args->offset = 0;
	if (args->flags != 0)
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
bcmv3d_get_bo_offset(struct drm_device *dev, void *data,
    struct drm_file *file)
{
	struct drm_v3d_get_bo_offset *args = data;
	struct drm_gem_object *obj;

	args->offset = 0;
	obj = drm_gem_object_lookup(file, args->handle);
	if (obj == NULL)
		return -ENOENT;
	args->offset = bcmv3d_to_bo(obj)->gpu;
	drm_gem_object_put_unlocked(obj);
	return 0;
}
