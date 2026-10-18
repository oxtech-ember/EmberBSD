/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* AI-assisted private frame preparation. No HVS access or publication. */
#include <sys/param.h>
#include <sys/kmem.h>
#include <sys/systm.h>
#include <linux/err.h>
#include <drm/drm_gem.h>
#include "bcmv3d_drv.h"
#include "bcmv3d_hvs_prepare.h"

int
bcmv3d_hvs_prepare(struct bcmv3d_device *sc, bus_dma_tag_t consumer,
    struct bcmv3d_hvs_frame **out)
{
	/* DRM headers reverse PAGE_MASK; match the native BO constructor. */
	const bus_size_t size = ((size_t)BCMV3D_HVS_BYTES + PAGE_SIZE - 1) &
	    ~((size_t)PAGE_SIZE - 1);
	struct bcmv3d_hvs_frame *frame;
	struct drm_gem_cma_object *cma;
	struct bcmv3d_bo *bo;
	bus_addr_t address;
	int error;

	if (sc == NULL || sc->drm == NULL || sc->dmat == NULL ||
	    consumer == NULL || out == NULL)
		return EINVAL;
	bo = bcmv3d_bo_alloc(sc, BCMV3D_HVS_BYTES);
	if (IS_ERR(bo))
		return -PTR_ERR(bo);
	cma = &bo->cma;
	if (cma->vaddr == NULL || cma->dmat != sc->dmat ||
	    cma->dmasize != size || cma->base.size != size ||
	    cma->dmamap == NULL || cma->dmamap->dm_nsegs != 1 ||
	    cma->dmamap->dm_mapsize != size ||
	    cma->dmamap->dm_segs[0].ds_len != size) {
		error = EINVAL;
		goto put;
	}
	frame = kmem_zalloc(sizeof(*frame), KM_SLEEP);
	error = bus_dmamap_create(consumer, cma->dmasize, 1, cma->dmasize,
	    0, BUS_DMA_WAITOK, &frame->map);
	if (error != 0)
		goto free;
	error = bus_dmamap_load(consumer, frame->map, cma->vaddr,
	    cma->dmasize, NULL, BUS_DMA_WAITOK | BUS_DMA_WRITE);
	if (error != 0)
		goto destroy;
	if (frame->map->dm_nsegs != 1 ||
	    frame->map->dm_mapsize != cma->dmasize ||
	    frame->map->dm_segs[0].ds_len != cma->dmasize) {
		error = EINVAL;
		goto unload;
	}
	address = frame->map->dm_segs[0].ds_addr;
	/* The current consumer tag is 32-bit. Zero and exclusive end are valid. */
	if (cma->dmasize > (UINT64_C(1) << 32) ||
	    address > (UINT64_C(1) << 32) - cma->dmasize) {
		error = EFBIG;
		goto unload;
	}
	frame->bo = bo;
	frame->dmat = consumer;
	frame->vaddr = cma->vaddr;
	frame->address = address;
	frame->size = cma->dmasize;
	frame->pitch = BCMV3D_HVS_PITCH;
	*out = frame;
	return 0;
unload:
	bus_dmamap_unload(consumer, frame->map);
destroy:
	bus_dmamap_destroy(consumer, frame->map);
free:
	kmem_free(frame, sizeof(*frame));
put:
	drm_gem_object_put_unlocked(&bo->cma.base);
	return error;
}

void
bcmv3d_hvs_prewrite(struct bcmv3d_hvs_frame *frame)
{

	bus_dmamap_sync(frame->dmat, frame->map, 0, frame->size,
	    BUS_DMASYNC_PREWRITE);
}

void
bcmv3d_hvs_destroy(struct bcmv3d_hvs_frame *frame)
{

	bus_dmamap_unload(frame->dmat, frame->map);
	bus_dmamap_destroy(frame->dmat, frame->map);
	drm_gem_object_put_unlocked(&frame->bo->cma.base);
	kmem_free(frame, sizeof(*frame));
}
