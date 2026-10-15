/* Origin: EmberBSD native BCM2712 GEM buffer ownership, 2026-10-11. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

/* The fault pager below derives from drm_gem_cma_helper.c, revision 1.15. */
/*-
 * Copyright (c) 2015-2017 Jared McNeill <jmcneill@invisible.ca>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/kmem.h>
#include <sys/systm.h>
#include <linux/err.h>
#include <uvm/uvm_extern.h>
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
	bus_dmamem_free(cma->dmat, bo->dmasegs, bo->nsegs);
	mutex_enter(&sc->memory_lock);
	vmem_free(sc->va, bo->gpu, cma->dmasize);
	KASSERT(sc->allocated >= cma->dmasize);
	sc->allocated -= cma->dmasize;
	mutex_exit(&sc->memory_lock);
	kmem_free(bo->dmasegs, bo->maxsegs * sizeof(*bo->dmasegs));
	kmem_free(bo, sizeof(*bo));
}

static const struct drm_gem_object_funcs bcmv3d_bo_funcs = {
	.free = bcmv3d_bo_free,
};

static struct bcmv3d_bo *
bcmv3d_bo_alloc_internal(struct bcmv3d_device *sc, uint32_t requested,
    bool contiguous)
{
	struct bcmv3d_bo *bo;
	struct drm_gem_cma_object *cma;
	bus_addr_t address;
	size_t size, remaining;
	int error, seg;

	if (requested == 0 || requested > BCMV3D_BO_MAX)
		return ERR_PTR(-EINVAL);
	size = ((size_t)requested + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
	bo = kmem_zalloc(sizeof(*bo), KM_SLEEP);
	cma = &bo->cma;
	cma->dmat = sc->dmat;
	cma->dmasize = size;
	bo->maxsegs = contiguous ? 1 : size / PAGE_SIZE;
	bo->dmasegs = kmem_zalloc(bo->maxsegs * sizeof(*bo->dmasegs),
	    KM_SLEEP);
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
	    bo->dmasegs, bo->maxsegs, &bo->nsegs, BUS_DMA_WAITOK);
	if (error != 0)
		goto release_va;
	/* Match the existing CMA pager's write-combining userspace mapping. */
	error = bus_dmamem_map(sc->dmat, bo->dmasegs, bo->nsegs, size,
	    &cma->vaddr, BUS_DMA_WAITOK | BUS_DMA_PREFETCHABLE);
	if (error != 0)
		goto free_dma;
	error = bus_dmamap_create(sc->dmat, size, bo->maxsegs, size, 0,
	    BUS_DMA_WAITOK, &cma->dmamap);
	if (error != 0)
		goto unmap;
	error = bus_dmamap_load(sc->dmat, cma->dmamap, cma->vaddr,
	    size, NULL, BUS_DMA_WAITOK);
	if (error != 0)
		goto destroy;
	if (cma->dmamap->dm_nsegs < 1 ||
	    cma->dmamap->dm_nsegs > bo->maxsegs) {
		error = EINVAL;
		goto unload;
	}
	remaining = size;
	for (seg = 0; seg < cma->dmamap->dm_nsegs; seg++) {
		bus_size_t length = cma->dmamap->dm_segs[seg].ds_len;

		address = cma->dmamap->dm_segs[seg].ds_addr;
		if (length == 0 || length > remaining ||
		    ((address | length) & (PAGE_SIZE - 1)) != 0 ||
		    address > (UINT64_C(1) << 36) - length) {
			error = EINVAL;
			goto unload;
		}
		remaining -= length;
	}
	if (remaining != 0) {
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
	bus_dmamem_free(sc->dmat, bo->dmasegs, bo->nsegs);
release_va:
	mutex_enter(&sc->memory_lock);
	vmem_free(sc->va, bo->gpu, size);
	sc->allocated -= size;
	mutex_exit(&sc->memory_lock);
free_bo:
	kmem_free(bo->dmasegs, bo->maxsegs * sizeof(*bo->dmasegs));
	kmem_free(bo, sizeof(*bo));
	return ERR_PTR(-error);
}

struct bcmv3d_bo *
bcmv3d_bo_alloc(struct bcmv3d_device *sc, uint32_t requested)
{

	return bcmv3d_bo_alloc_internal(sc, requested, true);
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
	bo = bcmv3d_bo_alloc_internal(dev->dev_private, args->size, false);
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

static int
bcmv3d_bo_fault(struct uvm_faultinfo *ufi, vaddr_t vaddr,
    struct vm_page **pps, int npages, int centeridx, vm_prot_t access_type,
    int flags)
{
	struct vm_map_entry *entry = ufi->entry;
	struct uvm_object *uobj = entry->object.uvm_obj;
	struct drm_gem_object *gem_obj =
	    container_of(uobj, struct drm_gem_object, gemo_uvmobj);
	struct bcmv3d_bo *bo = bcmv3d_to_bo(gem_obj);
	off_t curr_offset;
	vaddr_t curr_va;
	paddr_t paddr, mdpgno;
	u_int mmapflags;
	int lcv, retval;
	vm_prot_t mapprot;

	retval = EIO;
	if (UVM_ET_ISCOPYONWRITE(entry) || entry->offset < 0 ||
	    vaddr < entry->start || (uint64_t)entry->offset > gem_obj->size ||
	    vaddr - entry->start > gem_obj->size - (size_t)entry->offset)
		goto out;

	curr_offset = entry->offset + (vaddr - entry->start);
	curr_va = vaddr;

	retval = 0;
	for (lcv = 0; lcv < npages; lcv++, curr_offset += PAGE_SIZE,
	    curr_va += PAGE_SIZE) {
		if ((flags & PGO_ALLPAGES) == 0 && lcv != centeridx)
			continue;
		if (pps[lcv] == PGO_DONTCARE)
			continue;

		if (curr_offset < 0 || (uint64_t)curr_offset >= gem_obj->size) {
			retval = EIO;
			break;
		}
		mdpgno = bus_dmamem_mmap(bo->cma.dmat, bo->dmasegs, bo->nsegs,
		    curr_offset, access_type, BUS_DMA_PREFETCHABLE);
		if (mdpgno == (paddr_t)-1) {
			retval = EIO;
			break;
		}
		paddr = pmap_phys_address(mdpgno);
		mmapflags = pmap_mmap_flags(mdpgno);
		mapprot = ufi->entry->protection;

		if (pmap_enter(ufi->orig_map->pmap, curr_va, paddr, mapprot,
		    PMAP_CANFAIL | mapprot | mmapflags) != 0) {
			retval = ENOMEM;
			break;
		}
	}

out:
	pmap_update(ufi->orig_map->pmap);
	uvmfault_unlockall(ufi, ufi->entry->aref.ar_amap, uobj);

	return retval;
}

const struct uvm_pagerops bcmv3d_bo_uvm_ops = {
	.pgo_reference = drm_gem_pager_reference,
	.pgo_detach = drm_gem_pager_detach,
	.pgo_fault = bcmv3d_bo_fault,
};
