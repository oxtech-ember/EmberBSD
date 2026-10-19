/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* AI-assisted private native backing. No hardware publication or MMIO. */
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/cpu.h>
#include <sys/kmem.h>
#include <sys/systm.h>
#include <uvm/uvm_extern.h>
#include <machine/pmap.h>
#include <arm/broadcom/bcm2712_mmu4_corevar.h>
#include <arm/broadcom/bcm2712_mmu4_tablevar.h>
#include "bcmv3d_drv.h"
#include "bcmv3d_hvs_prepare.h"
#include "bcmv3d_mmu4_backing.h"

/*
 * Reuse native bus_dma/pmap and the fixed BSD encoder. The encoder's numeric
 * MMU facts derive from Linux a1a601d4857978d810175365550c4536701df4ed,
 * drivers/iommu/bcm2712-iommu.c. No GPL implementation is copied here.
 */
struct mmu4_allocation {
	bus_dma_segment_t segment;
	bus_dmamap_t map;
	void *kva;
	uint64_t pa;
	bool allocated, mapped, loaded;
};

struct bcmv3d_mmu4_backing {
	struct bcmv3d_device *sc;
	struct bcmv3d_hvs_frame *frame;
	struct mmu4_allocation control[4];
	struct bcm2712_mmu4_pages pages;
	struct bcm2712_mmu4_image image;
	struct bcm2712_mmu4_config cfg;
	struct bcmv3d_mmu4_backing_status status;
	bool running;
};

static bool
mmu4_overlap(uint64_t a, uint64_t asize, uint64_t b, uint64_t bsize)
{

	return a <= b ? b - a < asize : a - b < bsize;
}

static int
mmu4_map_check(bus_dmamap_t map, uint64_t size)
{

	if (map == NULL || map->dm_nsegs != 1 ||
	    map->dm_mapsize != size || map->dm_segs[0].ds_len != size)
		return EINVAL;
	if ((map->_dm_flags & _BUS_DMAMAP_IS_BOUNCING) != 0 ||
	    map->_dm_iommu != NULL)
		return EOPNOTSUPP;
	return 0;
}

static int
mmu4_physical(struct bcmv3d_mmu4_backing *b, void *kva, uint64_t size,
    bus_dmamap_t map, uint64_t *base)
{
	paddr_t first, last;
	uint64_t offset, start = 0;
	uintptr_t va = (uintptr_t)kva;
	bool coherent;
	int error;

	error = mmu4_map_check(map, size);
	if (error != 0)
		return error;
	if (kva == NULL || (va & (PAGE_SIZE - 1)) != 0 || size == 0 ||
	    size - 1 > UINTPTR_MAX - va)
		return EINVAL;
	for (offset = 0; offset < size; offset += BCM2712_MMU4_PAGE_BYTES) {
		++b->status.pmap_calls;
		if (!pmap_extract_coherency(pmap_kernel(), va + offset,
		    &first, &coherent))
			return EFAULT;
		++b->status.pmap_calls;
		if (!pmap_extract_coherency(pmap_kernel(), va + offset + 4095,
		    &last, &coherent))
			return EFAULT;
		if (offset == 0) {
			start = first;
			if ((start & (PAGE_SIZE - 1)) != 0)
				return EINVAL;
			if (size > (UINT64_C(1) << 32) ||
			    start > (UINT64_C(1) << 32) - size)
				return ERANGE;
			if (map->dm_segs[0].ds_addr != start)
				return EOPNOTSUPP;
		}
		if (first != start + offset || last != first + 4095)
			return EAGAIN;
		++b->status.fragments;
	}
	*base = start;
	return 0;
}

static int
mmu4_validate(struct bcmv3d_mmu4_backing *b, bool initial)
{
	struct drm_gem_cma_object *cma = &b->frame->bo->cma;
	uint64_t pa[5], size[5], cpu[5];
	unsigned int i, j;
	int error;

	b->status.stage = BCMV3D_MMU4_BACKING_VALIDATE;
	b->status.fragments = b->status.pmap_calls = 0;
	if (cma->base.dev != b->sc->drm || b->frame->bo->busy != 0 ||
	    b->frame->dmat != b->sc->dmat || cma->dmat != b->sc->dmat ||
	    b->frame->vaddr != cma->vaddr ||
	    b->frame->size != b->status.frame_bytes ||
	    cma->dmasize != b->status.frame_bytes ||
	    cma->base.size != b->status.frame_bytes ||
	    b->frame->pitch != BCMV3D_HVS_PITCH)
		return EINVAL;
	error = mmu4_map_check(cma->dmamap, b->status.frame_bytes);
	if (error != 0)
		return error;
	error = mmu4_physical(b, b->frame->vaddr, b->status.frame_bytes,
	    b->frame->map, &pa[0]);
	if (error != 0)
		return error;
	if (cma->dmamap->dm_segs[0].ds_addr != pa[0] ||
	    b->frame->address != pa[0])
		return EOPNOTSUPP;
	size[0] = b->status.frame_bytes;
	cpu[0] = (uintptr_t)b->frame->vaddr;
	for (i = 0; i < 4; i++) {
		error = mmu4_physical(b, b->control[i].kva,
		    b->status.control_bytes, b->control[i].map, &pa[i + 1]);
		if (error != 0)
			return error;
		if (pa[i + 1] == 0)
			return EINVAL;
		if (!initial && pa[i + 1] != b->control[i].pa)
			return EAGAIN;
		size[i + 1] = b->status.control_bytes;
		cpu[i + 1] = (uintptr_t)b->control[i].kva;
	}
	for (i = 0; i < 5; i++) {
		if (mmu4_overlap(cpu[i], size[i], (uintptr_t)b, sizeof(*b)))
			return EINVAL;
		for (j = 0; j < i; j++) {
			if (mmu4_overlap(pa[i], size[i], pa[j], size[j]) ||
			    mmu4_overlap(cpu[i], size[i], cpu[j], size[j]))
				return EINVAL;
		}
	}
	if (initial) {
		for (i = 0; i < BCM2712_MMU4_FRAME_PAGES; i++)
			b->pages.frame_pa[i] = pa[0] + i * UINT64_C(4096);
		for (i = 0; i < 4; i++)
			b->control[i].pa = pa[i + 1];
		b->pages.top_pa = b->cfg.top_pa = pa[1];
		b->pages.l2_pa[0] = b->cfg.l2_pa[0] = pa[2];
		b->pages.l2_pa[1] = b->cfg.l2_pa[1] = pa[3];
		b->pages.default_pa = b->cfg.default_pa = pa[4];
	} else if (pa[0] != b->pages.frame_pa[0]) {
		return EAGAIN;
	}
	return 0;
}

static void
mmu4_free(struct bcmv3d_mmu4_backing *b)
{
	struct mmu4_allocation *a;
	unsigned int i;

	for (i = 4; i > 0; i--) {
		a = &b->control[i - 1];
		if (a->loaded)
			bus_dmamap_unload(b->sc->dmat, a->map);
		if (a->map != NULL)
			bus_dmamap_destroy(b->sc->dmat, a->map);
		if (a->mapped)
			bus_dmamem_unmap(b->sc->dmat, a->kva,
			    b->status.control_bytes);
		if (a->allocated)
			bus_dmamem_free(b->sc->dmat, &a->segment, 1);
	}
	if (b->frame != NULL)
		bcmv3d_hvs_destroy(b->frame);
	kmem_free(b, sizeof(*b));
}

int
bcmv3d_mmu4_backing_create(struct bcmv3d_device *sc, uint32_t debug,
    struct bcmv3d_mmu4_backing **out)
{
	struct bcmv3d_mmu4_backing *b;
	struct mmu4_allocation *a;
	unsigned int i;
	int error, nsegments;

	if (sc == NULL || sc->drm == NULL || sc->dmat == NULL || out == NULL)
		return EINVAL;
	if (cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	ASSERT_SLEEPABLE();
	if (PAGE_SIZE != 4096 && PAGE_SIZE != 65536)
		return EOPNOTSUPP;
	b = kmem_zalloc(sizeof(*b), KM_SLEEP);
	b->sc = sc;
	b->status.frame_bytes = ((size_t)BCMV3D_HVS_BYTES + PAGE_SIZE - 1) &
	    ~((size_t)PAGE_SIZE - 1);
	b->status.control_bytes = PAGE_SIZE;
	b->pages.debug = b->cfg.debug = debug;
	error = bcmv3d_hvs_prepare(sc, sc->dmat, &b->frame);
	if (error != 0)
		goto fail;
	for (i = 0; i < 4; i++) {
		a = &b->control[i];
		error = bus_dmamem_alloc(sc->dmat, b->status.control_bytes,
		    PAGE_SIZE, 0, &a->segment, 1, &nsegments, BUS_DMA_WAITOK);
		if (error != 0)
			goto fail;
		a->allocated = true;
		if (nsegments != 1 ||
		    a->segment.ds_len != b->status.control_bytes) {
			error = EINVAL;
			goto fail;
		}
		error = bus_dmamem_map(sc->dmat, &a->segment, 1,
		    b->status.control_bytes, &a->kva, BUS_DMA_WAITOK);
		if (error != 0)
			goto fail;
		a->mapped = true;
		error = bus_dmamap_create(sc->dmat, b->status.control_bytes,
		    1, b->status.control_bytes, 0, BUS_DMA_WAITOK, &a->map);
		if (error != 0)
			goto fail;
		error = bus_dmamap_load(sc->dmat, a->map, a->kva,
		    b->status.control_bytes, NULL, BUS_DMA_WAITOK | BUS_DMA_WRITE);
		if (error != 0)
			goto fail;
		a->loaded = true;
	}
	error = mmu4_validate(b, true);
	if (error != 0)
		goto fail;
	error = bcm2712_mmu4_encode(&b->pages, &b->image);
	if (error != 0)
		goto fail;
	b->status.stage = BCMV3D_MMU4_BACKING_IDLE;
	*out = b;
	return 0;
fail:
	mmu4_free(b);
	return error;
}

int
bcmv3d_mmu4_backing_get_config(const struct bcmv3d_mmu4_backing *b,
    struct bcm2712_mmu4_config *out)
{

	if (b == NULL || out == NULL)
		return EINVAL;
	memcpy(out, &b->cfg, sizeof(*out));
	return 0;
}

/* Fixed experimental first frame. XRGB8888 bytes are B, G, R, zero. */
static void
mmu4_frame_init(void *vaddr)
{
	static const uint32_t colors[8] = {
		0x00ffffff, 0x00ffff00, 0x0000ffff, 0x0000ff00,
		0x00ff00ff, 0x00ff0000, 0x000000ff, 0x00000000
	};
	uint8_t *pixel;
	uint32_t color;
	unsigned int x, y;

	for (y = 0; y < BCMV3D_HVS_HEIGHT; y++) {
		pixel = (uint8_t *)vaddr + y * BCMV3D_HVS_PITCH;
		for (x = 0; x < BCMV3D_HVS_WIDTH; x++) {
			color = colors[x / (BCMV3D_HVS_WIDTH / 8)];
			pixel[0] = (uint8_t)color;
			pixel[1] = (uint8_t)(color >> 8);
			pixel[2] = (uint8_t)(color >> 16);
			pixel[3] = 0;
			pixel += 4;
		}
	}
}

int
bcmv3d_mmu4_backing_prepare(void *arg, const struct bcm2712_mmu4_config *cfg)
{
	struct bcmv3d_mmu4_backing *b = arg;
	const void *image[4];
	unsigned int i;
	int error;

	if (b == NULL || cfg == NULL)
		return EINVAL;
	if (cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	ASSERT_SLEEPABLE();
	if (b->running || b->status.prepare_started)
		return EBUSY;
	b->running = b->status.prepare_started = true;
	if (cfg->debug != b->cfg.debug || cfg->top_pa != b->cfg.top_pa ||
	    cfg->l2_pa[0] != b->cfg.l2_pa[0] ||
	    cfg->l2_pa[1] != b->cfg.l2_pa[1] ||
	    cfg->default_pa != b->cfg.default_pa) {
		error = EINVAL;
		goto done;
	}
	error = mmu4_validate(b, false);
	if (error != 0) {
		b->status.mapping_uncertain = true;
		goto done;
	}
	b->status.stage = BCMV3D_MMU4_BACKING_ENCODE;
	error = bcm2712_mmu4_encode(&b->pages, &b->image);
	if (error != 0)
		goto done;
	b->status.stage = BCMV3D_MMU4_BACKING_ZERO;
	memset(b->frame->vaddr, 0, b->status.frame_bytes);
	mmu4_frame_init(b->frame->vaddr);
	for (i = 0; i < 4; i++)
		memset(b->control[i].kva, 0, b->status.control_bytes);
	image[0] = b->image.top;
	image[1] = b->image.l2[0];
	image[2] = b->image.l2[1];
	image[3] = b->image.default_page;
	b->status.stage = BCMV3D_MMU4_BACKING_COPY;
	for (i = 0; i < 4; i++)
		memcpy(b->control[i].kva, image[i], BCM2712_MMU4_PAGE_BYTES);
	b->status.stage = BCMV3D_MMU4_BACKING_SYNC;
	for (i = 0; i < 4; i++) {
		bus_dmamap_sync(b->sc->dmat, b->control[i].map, 0,
		    b->status.control_bytes, BUS_DMASYNC_PREWRITE);
		++b->status.syncs;
	}
	bcmv3d_hvs_prewrite(b->frame);
	++b->status.syncs;
	b->status.prepared = true;
	b->status.stage = BCMV3D_MMU4_BACKING_PREPARED;
done:
	b->status.first_error = error;
	b->running = false;
	return error;
}

int
bcmv3d_mmu4_backing_status(const struct bcmv3d_mmu4_backing *b,
    struct bcmv3d_mmu4_backing_status *out)
{

	if (b == NULL || out == NULL)
		return EINVAL;
	memcpy(out, &b->status, sizeof(*out));
	return 0;
}

int
bcmv3d_mmu4_backing_seal(struct bcmv3d_mmu4_backing *b)
{

	if (b == NULL)
		return EINVAL;
	if (b->running || b->status.sealed)
		return EBUSY;
	if (b->status.first_error != 0)
		return EIO;
	b->status.sealed = true;
	return 0;
}

int
bcmv3d_mmu4_backing_destroy_unexposed(struct bcmv3d_mmu4_backing *b)
{

	if (b == NULL)
		return EINVAL;
	if (cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	ASSERT_SLEEPABLE();
	if (b->running || b->status.sealed || b->status.mapping_uncertain)
		return EBUSY;
	mmu4_free(b);
	return 0;
}
