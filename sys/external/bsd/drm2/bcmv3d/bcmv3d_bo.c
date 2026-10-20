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
#include <drm/drm_mode.h>
#include <drm/drm_vma_manager.h>
#include "bcmv3d_drv.h"
#include "bcmv3d_dumb.h"

/* All counters and snapshots share memory_lock with the quota reservation. */
static void
bcmv3d_bo_count(uint64_t *counter)
{

	if (*counter != UINT64_MAX)
		++*counter;
}

static void
bcmv3d_bo_failure_locked(struct bcmv3d_device *sc,
    enum bcmv3d_bo_failure_stage stage, uint32_t requested, size_t rounded,
    int error)
{
	struct bcmv3d_bo_stats *stats = &sc->bo_stats;

	KASSERT(stage > BCMV3D_BO_STAGE_NONE && stage < BCMV3D_BO_STAGE_COUNT);
	KASSERT(error > 0);
	bcmv3d_bo_count(&stats->failures);
	bcmv3d_bo_count(&stats->failures_by_stage[stage]);
	stats->last_requested_bytes = requested;
	stats->last_rounded_bytes = rounded;
	stats->last_reserved_bytes = sc->allocated;
	stats->last_stage = stage;
	stats->last_errno = error;
}

static void
bcmv3d_bo_failure(struct bcmv3d_device *sc,
    enum bcmv3d_bo_failure_stage stage, uint32_t requested, size_t rounded,
    int error)
{

	mutex_enter(&sc->memory_lock);
	bcmv3d_bo_failure_locked(sc, stage, requested, rounded, error);
	mutex_exit(&sc->memory_lock);
}

void
bcmv3d_bo_snapshot(struct bcmv3d_device *sc, struct bcmv3d_bo_stats *snapshot)
{

	mutex_enter(&sc->memory_lock);
	*snapshot = sc->bo_stats;
	snapshot->version = BCMV3D_BO_STATS_VERSION;
	snapshot->length = sizeof(*snapshot);
	snapshot->reserved_bytes = sc->allocated;
	snapshot->limit_bytes = sc->bo_total;
	snapshot->max_bo_bytes = BCMV3D_BO_MAX;
	mutex_exit(&sc->memory_lock);
}

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
	bcmv3d_bo_count(&sc->bo_stats.releases);
	mutex_exit(&sc->memory_lock);
	kmem_free(bo->dmasegs, bo->maxsegs * sizeof(*bo->dmasegs));
	kmem_free(bo, sizeof(*bo));
}

static const struct drm_gem_object_funcs bcmv3d_bo_funcs = {
	.free = bcmv3d_bo_free,
};

/* The caller holds a live GEM reference through identity and acquisition. */
int
bcmv3d_bo_reference(struct bcmv3d_device *sc, struct drm_gem_object *obj,
    struct bcmv3d_bo **out)
{

	if (sc == NULL || sc->drm == NULL || obj == NULL || out == NULL)
		return EINVAL;
	if (obj->dev != sc->drm || obj->funcs != &bcmv3d_bo_funcs)
		return ENXIO;
	drm_gem_object_get(obj);
	*out = bcmv3d_to_bo(obj);
	return 0;
}

static struct bcmv3d_bo *
bcmv3d_bo_alloc_internal(struct bcmv3d_device *sc, uint32_t requested,
    bool contiguous)
{
	struct bcmv3d_bo *bo;
	struct drm_gem_cma_object *cma;
	bus_addr_t address;
	size_t size, remaining;
	int error, seg;

	if (requested == 0 || requested > BCMV3D_BO_MAX) {
		bcmv3d_bo_failure(sc, BCMV3D_BO_STAGE_REQUEST, requested, 0,
		    EINVAL);
		return ERR_PTR(-EINVAL);
	}
	size = ((size_t)requested + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
	bo = kmem_zalloc(sizeof(*bo), KM_SLEEP);
	cma = &bo->cma;
	cma->dmat = sc->dmat;
	cma->dmasize = size;
	bo->maxsegs = contiguous ? 1 : size / PAGE_SIZE;
	mutex_enter(&sc->memory_lock);
	if (size > sc->bo_total - sc->allocated) {
		bcmv3d_bo_failure_locked(sc, BCMV3D_BO_STAGE_QUOTA,
		    requested, size, ENOMEM);
		mutex_exit(&sc->memory_lock);
		error = ENOMEM;
		goto free_bo;
	}
	error = vmem_alloc(sc->va, size, VM_BESTFIT | VM_NOSLEEP, &bo->gpu);
	if (error != 0) {
		bcmv3d_bo_failure_locked(sc, BCMV3D_BO_STAGE_VA,
		    requested, size, error);
		mutex_exit(&sc->memory_lock);
		goto free_bo;
	}
	sc->allocated += size;
	bcmv3d_bo_count(&sc->bo_stats.reservations);
	if (sc->allocated > sc->bo_stats.highwater_bytes)
		sc->bo_stats.highwater_bytes = sc->allocated;
	mutex_exit(&sc->memory_lock);

	/* Reserve backing before allocating its potentially large segment list. */
	bo->dmasegs = kmem_zalloc(bo->maxsegs * sizeof(*bo->dmasegs),
	    KM_SLEEP);

	error = bus_dmamem_alloc(sc->dmat, size, PAGE_SIZE, 0,
	    bo->dmasegs, bo->maxsegs, &bo->nsegs, BUS_DMA_WAITOK);
	if (error != 0) {
		bcmv3d_bo_failure(sc, BCMV3D_BO_STAGE_DMA_ALLOC, requested,
		    size, error);
		goto release_va;
	}
	/* Match the existing CMA pager's write-combining userspace mapping. */
	error = bus_dmamem_map(sc->dmat, bo->dmasegs, bo->nsegs, size,
	    &cma->vaddr, BUS_DMA_WAITOK | BUS_DMA_PREFETCHABLE);
	if (error != 0) {
		bcmv3d_bo_failure(sc, BCMV3D_BO_STAGE_DMA_MAP, requested,
		    size, error);
		goto free_dma;
	}
	error = bus_dmamap_create(sc->dmat, size, bo->maxsegs, size, 0,
	    BUS_DMA_WAITOK, &cma->dmamap);
	if (error != 0) {
		bcmv3d_bo_failure(sc, BCMV3D_BO_STAGE_MAP_CREATE, requested,
		    size, error);
		goto unmap;
	}
	error = bus_dmamap_load(sc->dmat, cma->dmamap, cma->vaddr,
	    size, NULL, BUS_DMA_WAITOK);
	if (error != 0) {
		bcmv3d_bo_failure(sc, BCMV3D_BO_STAGE_MAP_LOAD, requested,
		    size, error);
		goto destroy;
	}
	if (cma->dmamap->dm_nsegs < 1 ||
	    cma->dmamap->dm_nsegs > bo->maxsegs) {
		error = EINVAL;
		bcmv3d_bo_failure(sc, BCMV3D_BO_STAGE_GEOMETRY, requested,
		    size, error);
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
			bcmv3d_bo_failure(sc, BCMV3D_BO_STAGE_GEOMETRY,
			    requested, size, error);
			goto unload;
		}
		remaining -= length;
	}
	if (remaining != 0) {
		error = EINVAL;
		bcmv3d_bo_failure(sc, BCMV3D_BO_STAGE_GEOMETRY, requested,
		    size, error);
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
	bcmv3d_bo_count(&sc->bo_stats.releases);
	mutex_exit(&sc->memory_lock);
free_bo:
	if (bo->dmasegs != NULL)
		kmem_free(bo->dmasegs, bo->maxsegs * sizeof(*bo->dmasegs));
	kmem_free(bo, sizeof(*bo));
	return ERR_PTR(-error);
}

struct bcmv3d_bo *
bcmv3d_bo_alloc(struct bcmv3d_device *sc, uint32_t requested)
{

	return bcmv3d_bo_alloc_internal(sc, requested, true);
}

/* Generic CPU-written buffers use the same private native GEM allocator. */
int
bcmv3d_dumb_create(struct drm_file *file, struct drm_device *dev,
    struct drm_mode_create_dumb *args)
{
	struct bcmv3d_bo *bo;
	uint64_t pitch, size;
	uint32_t handle;
	int error;

	args->handle = 0;
	args->pitch = 0;
	args->size = 0;
	if (args->bpp != 32 || args->flags != 0 ||
	    args->width == 0 || args->height == 0)
		return -EINVAL;
	pitch = ((uint64_t)args->width * 4 + 63) & ~UINT64_C(63);
	if (pitch > UINT64_MAX / args->height)
		return -EINVAL;
	size = pitch * args->height;
	if (pitch > UINT32_MAX || size > BCMV3D_BO_MAX)
		return -EINVAL;
	bo = bcmv3d_bo_alloc_internal(dev->dev_private, (uint32_t)size, false);
	if (IS_ERR(bo))
		return PTR_ERR(bo);
	error = drm_gem_handle_create(file, &bo->cma.base, &handle);
	if (error == 0) {
		args->handle = handle;
		args->pitch = (uint32_t)pitch;
		args->size = bo->cma.base.size;
	} else {
		bcmv3d_bo_failure(dev->dev_private, BCMV3D_BO_STAGE_HANDLE,
		    (uint32_t)size, bo->cma.dmasize, -error);
	}
	/* The handle owns its reference; release the constructor on both paths. */
	drm_gem_object_put_unlocked(&bo->cma.base);
	return error;
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
	if (args->flags != 0) {
		bcmv3d_bo_failure(dev->dev_private, BCMV3D_BO_STAGE_REQUEST,
		    args->size, 0, EINVAL);
		return -EINVAL;
	}
	bo = bcmv3d_bo_alloc_internal(dev->dev_private, args->size, false);
	if (IS_ERR(bo))
		return PTR_ERR(bo);
	error = drm_gem_handle_create(file, &bo->cma.base, &handle);
	if (error == 0) {
		args->handle = handle;
		args->offset = bo->gpu;
	} else {
		bcmv3d_bo_failure(dev->dev_private, BCMV3D_BO_STAGE_HANDLE,
		    args->size, bo->cma.dmasize, -error);
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
