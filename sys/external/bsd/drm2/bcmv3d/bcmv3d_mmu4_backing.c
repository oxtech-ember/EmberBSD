/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* AI-assisted private native backing and table updates. No MMIO. */
#include "opt_wsdisplay_bootfb.h"
#include <sys/param.h>
#include <sys/atomic.h>
#include <sys/endian.h>
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
#ifdef WSDISPLAY_BOOTFB_HANDOFF
#include "bcmv3d_scanout.h"
#endif

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

/* Only private, never-published storage. No hardware owns these slots. */
struct mmu4_gem_slot {
	struct bcmv3d_bo *bo;
	bus_dmamap_t map;
	struct mmu4_allocation control[2];
	struct bcm2712_mmu4_range_pages pages;
	struct bcm2712_mmu4_range_image image;
	bool loaded, linked, published, read_synced, exposed, retired;
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
	int range_fault;
	struct mmu4_gem_slot *staged[BCM2712_MMU4_SCANOUT_SLOTS];
	uint8_t candidate_top[BCM2712_MMU4_PAGE_BYTES];
};

static void mmu4_gem_free(struct bcmv3d_mmu4_backing *,
    struct mmu4_gem_slot *);

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

	for (i = BCM2712_MMU4_SCANOUT_SLOTS; i > 0; i--) {
		if (b->staged[i - 1] != NULL)
			mmu4_gem_free(b, b->staged[i - 1]);
	}
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

static int
mmu4_gem_map(bus_dmamap_t map, uint64_t size, unsigned int maxsegs)
{
	uint64_t remaining;
	bus_addr_t pa;
	bus_size_t length;
	unsigned int i, j;

	if (map == NULL || map->dm_mapsize != size || map->dm_nsegs <= 0 ||
	    (unsigned int)map->dm_nsegs > maxsegs)
		return EINVAL;
	if ((map->_dm_flags & _BUS_DMAMAP_IS_BOUNCING) != 0 ||
	    map->_dm_iommu != NULL)
		return EOPNOTSUPP;
	remaining = size;
	for (i = 0; i < (unsigned int)map->dm_nsegs; i++) {
		pa = map->dm_segs[i].ds_addr;
		length = map->dm_segs[i].ds_len;
		if (length == 0 || length > remaining ||
		    ((pa | length) & (BCM2712_MMU4_PAGE_BYTES - 1)) != 0)
			return EINVAL;
		if (length > (UINT64_C(1) << 32) ||
		    pa > (UINT64_C(1) << 32) - length)
			return ERANGE;
		for (j = 0; j < i; j++) {
			if (mmu4_overlap(pa, length, map->dm_segs[j].ds_addr,
			    map->dm_segs[j].ds_len))
				return EINVAL;
		}
		remaining -= length;
	}
	return remaining == 0 ? 0 : EINVAL;
}

/* Check every native allocation byte through its 4 KiB fragment endpoints. */
static int
mmu4_gem_control(struct mmu4_allocation *a, uint64_t size, uint64_t *out)
{
	uintptr_t va = (uintptr_t)a->kva;
	uint64_t offset, start;
	paddr_t first, last;
	bool coherent;
	int error;

	error = mmu4_map_check(a->map, size);
	if (error != 0)
		return error;
	if (a->kva == NULL || (va & (PAGE_SIZE - 1)) != 0 ||
	    size == 0 || size - 1 > UINTPTR_MAX - va)
		return EINVAL;
	start = a->map->dm_segs[0].ds_addr;
	if (start == 0 || (start & (PAGE_SIZE - 1)) != 0)
		return EINVAL;
	if (size > (UINT64_C(1) << 32) ||
	    start > (UINT64_C(1) << 32) - size)
		return ERANGE;
	for (offset = 0; offset < size; offset += BCM2712_MMU4_PAGE_BYTES) {
		if (!pmap_extract_coherency(pmap_kernel(), va + offset,
		    &first, &coherent) ||
		    !pmap_extract_coherency(pmap_kernel(), va + offset + 4095,
		    &last, &coherent))
			return EFAULT;
		if (first != start + offset || last != first + 4095)
			return EAGAIN;
	}
	*out = start;
	return 0;
}

static int
mmu4_gem_pages(struct mmu4_gem_slot *g, bool record)
{
	struct drm_gem_cma_object *cma = &g->bo->cma;
	bus_dmamap_t parent = cma->dmamap;
	uintptr_t va = (uintptr_t)cma->vaddr;
	uint64_t offset, poff = 0, coff = 0;
	unsigned int pseg = 0, cseg = 0;
	paddr_t first, last;
	bool coherent;

	for (offset = 0; offset < cma->dmasize;
	    offset += BCM2712_MMU4_PAGE_BYTES) {
		if (!pmap_extract_coherency(pmap_kernel(), va + offset,
		    &first, &coherent) ||
		    !pmap_extract_coherency(pmap_kernel(), va + offset + 4095,
		    &last, &coherent))
			return EFAULT;
		if ((first & (BCM2712_MMU4_PAGE_BYTES - 1)) != 0 ||
		    last != first + 4095)
			return EAGAIN;
		if (first != parent->dm_segs[pseg].ds_addr + poff ||
		    first != g->map->dm_segs[cseg].ds_addr + coff)
			return EOPNOTSUPP;
		if (offset < BCMV3D_HVS_BYTES) {
			if (record)
				g->pages.frame_pa[offset / BCM2712_MMU4_PAGE_BYTES] = first;
			else if (g->pages.frame_pa[offset /
			    BCM2712_MMU4_PAGE_BYTES] != first)
				return EAGAIN;
		}
		poff += BCM2712_MMU4_PAGE_BYTES;
		coff += BCM2712_MMU4_PAGE_BYTES;
		if (poff == parent->dm_segs[pseg].ds_len) {
			poff = 0;
			pseg++;
		}
		if (coff == g->map->dm_segs[cseg].ds_len) {
			coff = 0;
			cseg++;
		}
	}
	return 0;
}

static bool
mmu4_gem_map_overlap(bus_dmamap_t map, uint64_t pa, uint64_t size)
{
	int i;

	for (i = 0; i < map->dm_nsegs; i++) {
		if (mmu4_overlap(pa, size, map->dm_segs[i].ds_addr,
		    map->dm_segs[i].ds_len))
			return true;
	}
	return false;
}

static bool
mmu4_gem_allocation_overlap(const struct mmu4_allocation *a,
    const struct mmu4_allocation *other, uint64_t size)
{

	return mmu4_overlap(a->pa, size, other->pa, size) ||
	    mmu4_overlap((uintptr_t)a->kva, size, (uintptr_t)other->kva, size);
}

static int
mmu4_gem_aliases(struct bcmv3d_mmu4_backing *b, struct mmu4_gem_slot *g,
    const struct mmu4_gem_slot *skip)
{
	struct drm_gem_cma_object *cma = &g->bo->cma;
	struct mmu4_gem_slot *other;
	struct mmu4_allocation *a;
	uint64_t size = b->status.control_bytes;
	unsigned int i, j, k;
	int seg;

	if (mmu4_overlap((uintptr_t)cma->vaddr, cma->dmasize,
	    (uintptr_t)b->frame->vaddr, b->frame->size) ||
	    mmu4_gem_map_overlap(cma->dmamap, b->pages.frame_pa[0],
	    b->status.frame_bytes))
		return EINVAL;
	for (i = 0; i < 4; i++) {
		a = &b->control[i];
		if (mmu4_overlap((uintptr_t)cma->vaddr, cma->dmasize,
		    (uintptr_t)a->kva, size) ||
		    mmu4_gem_map_overlap(cma->dmamap, a->pa, size))
			return EINVAL;
	}
	for (i = 0; i < 2; i++) {
		a = &g->control[i];
		if (mmu4_overlap((uintptr_t)a->kva, size,
		    (uintptr_t)cma->vaddr, cma->dmasize) ||
		    mmu4_overlap((uintptr_t)a->kva, size,
		    (uintptr_t)b->frame->vaddr, b->frame->size) ||
		    mmu4_overlap(a->pa, size, b->pages.frame_pa[0],
		    b->status.frame_bytes) ||
		    mmu4_gem_map_overlap(cma->dmamap, a->pa, size))
			return EINVAL;
		for (j = 0; j < i; j++) {
			if (mmu4_gem_allocation_overlap(a, &g->control[j], size))
				return EINVAL;
		}
		for (j = 0; j < 4; j++) {
			if (mmu4_gem_allocation_overlap(a, &b->control[j], size))
				return EINVAL;
		}
	}
	for (i = 0; i < BCM2712_MMU4_SCANOUT_SLOTS; i++) {
		other = b->staged[i];
		if (other == NULL || other == skip)
			continue;
		if (other->bo != NULL &&
		    mmu4_overlap((uintptr_t)cma->vaddr, cma->dmasize,
		    (uintptr_t)other->bo->cma.vaddr, other->bo->cma.dmasize))
			return EINVAL;
		for (seg = 0; seg < cma->dmamap->dm_nsegs; seg++) {
			if (other->map != NULL &&
			    mmu4_gem_map_overlap(other->map,
			    cma->dmamap->dm_segs[seg].ds_addr,
			    cma->dmamap->dm_segs[seg].ds_len))
				return EINVAL;
		}
		for (j = 0; j < 2; j++) {
			if (mmu4_gem_map_overlap(cma->dmamap,
			    other->control[j].pa, size))
				return EINVAL;
			for (k = 0; k < 2; k++) {
				if (mmu4_gem_allocation_overlap(&g->control[k],
				    &other->control[j], size) ||
				    (other->map != NULL &&
				    mmu4_gem_map_overlap(other->map, g->control[k].pa, size)))
					return EINVAL;
			}
		}
	}
	return 0;
}

/* Pair only the actual consumer map. Its renderer map is a different object. */
static void
mmu4_gem_postwrite(struct bcmv3d_mmu4_backing *b, struct mmu4_gem_slot *g)
{

	KASSERT(!g->exposed || g->retired);
	if (!g->read_synced)
		return;
	bus_dmamap_sync(b->sc->dmat, g->map, 0, g->bo->cma.dmasize,
	    BUS_DMASYNC_POSTWRITE);
	g->read_synced = false;
}

static void
mmu4_gem_free(struct bcmv3d_mmu4_backing *b, struct mmu4_gem_slot *g)
{
	struct mmu4_allocation *a;
	unsigned int i;

	mmu4_gem_postwrite(b, g);
	for (i = 2; i > 0; i--) {
		a = &g->control[i - 1];
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
	if (g->loaded)
		bus_dmamap_unload(b->sc->dmat, g->map);
	if (g->map != NULL)
		bus_dmamap_destroy(b->sc->dmat, g->map);
	if (g->bo != NULL)
		drm_gem_object_put_unlocked(&g->bo->cma.base);
	kmem_free(g, sizeof(*g));
}

static void
mmu4_gem_drop_bo(struct bcmv3d_mmu4_backing *b, struct mmu4_gem_slot *g)
{

	mmu4_gem_postwrite(b, g);
	if (g->loaded)
		bus_dmamap_unload(b->sc->dmat, g->map);
	if (g->map != NULL)
		bus_dmamap_destroy(b->sc->dmat, g->map);
	if (g->bo != NULL)
		drm_gem_object_put_unlocked(&g->bo->cma.base);
	g->loaded = false;
	g->map = NULL;
	g->bo = NULL;
	memset(g->pages.frame_pa, 0, sizeof(g->pages.frame_pa));
	memset(g->image.l2, 0, sizeof(g->image.l2));
}

int
bcmv3d_mmu4_backing_stage_gem(struct bcmv3d_mmu4_backing *b, unsigned int slot,
    struct drm_gem_object *obj)
{
	struct mmu4_gem_slot *g, *old;
	struct drm_gem_cma_object *cma;
	struct mmu4_allocation *a;
	uint64_t pa;
	uintptr_t va;
	unsigned int i, maxsegs;
	int error, nsegments;

	if (b == NULL || obj == NULL || slot >= BCM2712_MMU4_SCANOUT_SLOTS)
		return EINVAL;
	if (cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	ASSERT_SLEEPABLE();
	if (b->range_fault != 0)
		return b->range_fault;
	old = b->staged[slot];
	if (b->running || !b->status.prepared || b->status.first_error != 0 ||
	    b->status.mapping_uncertain ||
	    (old != NULL && (!old->linked || old->bo != NULL || old->published)))
		return EBUSY;
	for (i = 0; i < BCM2712_MMU4_SCANOUT_SLOTS; i++) {
		if (b->staged[i] != NULL && b->staged[i]->bo != NULL &&
		    &b->staged[i]->bo->cma.base == obj)
			return EEXIST;
	}
	b->running = true;
	g = kmem_zalloc(sizeof(*g), KM_SLEEP);
	error = bcmv3d_bo_reference(b->sc, obj, &g->bo);
	if (error != 0)
		goto fail;
	cma = &g->bo->cma;
	va = (uintptr_t)cma->vaddr;
	if (cma->dmat != b->sc->dmat || cma->base.size != cma->dmasize ||
	    cma->dmasize < BCMV3D_HVS_BYTES || cma->dmasize > BCMV3D_BO_MAX ||
	    (cma->dmasize & (PAGE_SIZE - 1)) != 0 || cma->vaddr == NULL ||
	    (va & (PAGE_SIZE - 1)) != 0 ||
	    cma->dmasize - 1 > UINTPTR_MAX - va || g->bo->maxsegs <= 0 ||
	    g->bo->nsegs <= 0 || g->bo->nsegs > g->bo->maxsegs) {
		error = EINVAL;
		goto fail;
	}
	error = mmu4_gem_map(cma->dmamap, cma->dmasize, g->bo->maxsegs);
	if (error != 0)
		goto fail;
	maxsegs = cma->dmasize / PAGE_SIZE;
	error = bus_dmamap_create(b->sc->dmat, cma->dmasize, maxsegs,
	    cma->dmasize, 0, BUS_DMA_WAITOK, &g->map);
	if (error != 0)
		goto fail;
	error = bus_dmamap_load(b->sc->dmat, g->map, cma->vaddr,
	    cma->dmasize, NULL, BUS_DMA_WAITOK | BUS_DMA_WRITE);
	if (error != 0)
		goto fail;
	g->loaded = true;
	error = mmu4_gem_map(g->map, cma->dmasize, maxsegs);
	if (error != 0)
		goto fail;
	error = mmu4_gem_pages(g, true);
	if (error != 0)
		goto fail;
	/* Read-only proofs leave all initial plan/image/status bytes intact. */
	for (i = 0; i < 4; i++) {
		error = mmu4_gem_control(&b->control[i],
		    b->status.control_bytes, &pa);
		if (error != 0)
			goto fail;
		if (pa != b->control[i].pa) {
			error = EAGAIN;
			goto fail;
		}
	}
	for (i = 0; i < 2; i++) {
		if (old != NULL) {
			g->control[i] = old->control[i];
			error = mmu4_gem_control(&g->control[i],
			    b->status.control_bytes, &pa);
			if (error != 0)
				goto fail;
			if (pa != old->control[i].pa) {
				error = EAGAIN;
				goto fail;
			}
			g->pages.l2_pa[i] = pa;
			continue;
		}
		a = &g->control[i];
		error = bus_dmamem_alloc(b->sc->dmat, b->status.control_bytes,
		    PAGE_SIZE, 0, &a->segment, 1, &nsegments, BUS_DMA_WAITOK);
		if (error != 0)
			goto fail;
		a->allocated = true;
		if (nsegments != 1 || a->segment.ds_len != b->status.control_bytes) {
			error = EINVAL;
			goto fail;
		}
		error = bus_dmamem_map(b->sc->dmat, &a->segment, 1,
		    b->status.control_bytes, &a->kva, BUS_DMA_WAITOK);
		if (error != 0)
			goto fail;
		a->mapped = true;
		error = bus_dmamap_create(b->sc->dmat, b->status.control_bytes, 1,
		    b->status.control_bytes, 0, BUS_DMA_WAITOK, &a->map);
		if (error != 0)
			goto fail;
		error = bus_dmamap_load(b->sc->dmat, a->map, a->kva,
		    b->status.control_bytes, NULL, BUS_DMA_WAITOK | BUS_DMA_WRITE);
		if (error != 0)
			goto fail;
		a->loaded = true;
		error = mmu4_gem_control(a, b->status.control_bytes, &a->pa);
		if (error != 0)
			goto fail;
		g->pages.l2_pa[i] = a->pa;
	}
	error = mmu4_gem_aliases(b, g, old);
	if (error != 0)
		goto fail;
	g->pages.debug = b->cfg.debug;
	g->pages.top_pa = b->cfg.top_pa;
	error = bcm2712_mmu4_encode_range(&g->pages, slot, &g->image);
	if (error != 0)
		goto fail;
	for (i = 0; old == NULL && i < 2; i++) {
		memset(g->control[i].kva, 0, b->status.control_bytes);
		memcpy(g->control[i].kva, g->image.l2[i],
		    BCM2712_MMU4_PAGE_BYTES);
	}
	if (b->staged[0] == NULL && b->staged[1] == NULL && b->staged[2] == NULL)
		memcpy(b->candidate_top, b->image.top, sizeof(b->candidate_top));
	memcpy(b->candidate_top + (slot + 1) * 8, g->image.top, 8);
	if (old != NULL) {
		g->linked = true;
		*old = *g;
		kmem_free(g, sizeof(*g));
	} else {
		b->staged[slot] = g;
	}
	b->running = false;
	return 0;
fail:
	if (old != NULL)
		memset(g->control, 0, sizeof(g->control));
	mmu4_gem_free(b, g);
	b->running = false;
	return error;
}

int
bcmv3d_mmu4_backing_discard_staged_gem(struct bcmv3d_mmu4_backing *b,
    unsigned int slot)
{
	struct mmu4_gem_slot *g;

	if (b == NULL || slot >= BCM2712_MMU4_SCANOUT_SLOTS)
		return EINVAL;
	if (cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	ASSERT_SLEEPABLE();
	if (b->running)
		return EBUSY;
	g = b->staged[slot];
	if (g == NULL)
		return ENOENT;
	if (g->linked && g->bo == NULL)
		return ENOENT;
	if (g->published || g->exposed ||
	    (g->linked && b->range_fault != 0))
		return EBUSY;
	if (g->linked) {
		mmu4_gem_drop_bo(b, g);
		return 0;
	}
	b->running = true;
	mmu4_gem_free(b, g);
	memset(b->candidate_top + (slot + 1) * 8, 0, 8);
	b->staged[slot] = NULL;
	b->running = false;
	return 0;
}

static uint32_t
mmu4_range_word(const uint8_t *p)
{

	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	    ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int
bcmv3d_mmu4_backing_range_check(struct bcmv3d_mmu4_backing *b,
    unsigned int slot, enum bcmv3d_mmu4_range_operation op)
{
	struct mmu4_gem_slot *g, *other;
	uint64_t pa;
	uint32_t expected, actual;
	unsigned int i, j;
	int error;

	if (b == NULL || slot >= BCM2712_MMU4_SCANOUT_SLOTS ||
	    (op != BCMV3D_MMU4_RANGE_PUBLISH && op != BCMV3D_MMU4_RANGE_UNMAP))
		return EINVAL;
	if (cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	ASSERT_SLEEPABLE();
	if (b->range_fault != 0)
		return b->range_fault;
	if (b->running || !b->status.prepared || !b->status.sealed ||
	    b->status.first_error != 0 || b->status.mapping_uncertain)
		return EBUSY;
	g = b->staged[slot];
	if (g == NULL || g->bo == NULL)
		return ENOENT;
	/* No translation removal can authorize or substitute for retirement. */
	if (g->exposed && (!g->retired || op != BCMV3D_MMU4_RANGE_UNMAP))
		return EBUSY;
	if (op == BCMV3D_MMU4_RANGE_PUBLISH ? g->published : !g->published)
		return EBUSY;
	error = mmu4_gem_map(g->bo->cma.dmamap, g->bo->cma.dmasize,
	    g->bo->maxsegs);
	if (error == 0)
		error = mmu4_gem_map(g->map, g->bo->cma.dmasize,
		    g->bo->cma.dmasize / PAGE_SIZE);
	if (error == 0)
		error = mmu4_gem_pages(g, false);
	if (error != 0)
		return error;
	for (i = 0; i < 4; i++) {
		error = mmu4_gem_control(&b->control[i],
		    b->status.control_bytes, &pa);
		if (error != 0)
			return error;
		if (pa != b->control[i].pa)
			return EAGAIN;
	}
	for (i = 0; i < 2; i++) {
		error = mmu4_gem_control(&g->control[i],
		    b->status.control_bytes, &pa);
		if (error != 0)
			return error;
		if (pa != g->pages.l2_pa[i])
			return EAGAIN;
		for (j = 0; j < b->status.control_bytes / 4; j++) {
			expected = j < 1024 && (!g->linked || g->published) ?
			    mmu4_range_word(g->image.l2[i] + j * 4) : 0;
			actual = le32toh(atomic_load_relaxed(
			    (uint32_t *)g->control[i].kva + j));
			if (actual != expected)
				return EAGAIN;
		}
	}
	for (i = 0; i < b->status.control_bytes / 4; i++) {
		expected = i < 1024 ? mmu4_range_word(b->image.top + i * 4) : 0;
		if (i >= 2 && i < 8) {
			other = b->staged[i / 2 - 1];
			if (other != NULL && other->linked)
				expected = mmu4_range_word(other->image.top + (i % 2) * 4);
		}
		actual = le32toh(atomic_load_relaxed(
		    (uint32_t *)b->control[0].kva + i));
		if (actual != expected)
			return EAGAIN;
	}
	return mmu4_gem_aliases(b, g, g);
}

int
bcmv3d_mmu4_backing_range(struct bcmv3d_mmu4_backing *b, unsigned int slot,
    enum bcmv3d_mmu4_range_operation op, int (*invalidate)(void *), void *arg,
    struct bcmv3d_mmu4_range_status *s)
{
	struct mmu4_gem_slot *g;
	struct mmu4_allocation *a;
	uint32_t value;
	unsigned int i, j;
	int error;

	if (invalidate == NULL || s == NULL)
		return EINVAL;
	error = bcmv3d_mmu4_backing_range_check(b, slot, op);
	if (error != 0)
		return error;
	g = b->staged[slot];
	b->running = true;
	s->attempted = true;
	s->operation = op;
	s->slot = slot;
	/* A reachable leaf can be observed even before its explicit PREWRITE. */
	if (g->linked)
		s->mutation_attempted = true;
	for (i = 0; i < 2; i++) {
		a = &g->control[i];
		bus_dmamap_sync(b->sc->dmat, a->map, 0,
		    b->status.control_bytes, BUS_DMASYNC_POSTWRITE);
		s->syncs++;
		for (j = 0; j < 1024; j++) {
			value = op == BCMV3D_MMU4_RANGE_PUBLISH ?
			    mmu4_range_word(g->image.l2[i] + j * 4) : 0;
			atomic_store_relaxed((uint32_t *)a->kva + j, htole32(value));
			s->stores++;
		}
		bus_dmamap_sync(b->sc->dmat, a->map, 0,
		    b->status.control_bytes, BUS_DMASYNC_PREWRITE);
		s->syncs++;
	}
	if (!g->linked) {
		a = &b->control[0];
		bus_dmamap_sync(b->sc->dmat, a->map, 0,
		    b->status.control_bytes, BUS_DMASYNC_POSTWRITE);
		s->syncs++;
		/* Retain both tables and the BO BEFORE the first top PTE store. */
		g->linked = true;
		s->mutation_attempted = true;
		for (i = 0; i < 2; i++) {
			value = mmu4_range_word(g->image.top + i * 4);
			atomic_store_relaxed((uint32_t *)a->kva +
			    (slot + 1) * 2 + i, htole32(value));
			s->stores++;
		}
		bus_dmamap_sync(b->sc->dmat, a->map, 0,
		    b->status.control_bytes, BUS_DMASYNC_PREWRITE);
		s->syncs++;
	}
	error = invalidate(arg);
	if (error != 0) {
		s->first_error = b->range_fault = error;
	} else {
		g->published = op == BCMV3D_MMU4_RANGE_PUBLISH;
		s->complete = true;
	}
	b->running = false;
	return error;
}

int
bcmv3d_mmu4_backing_reclaim_unmapped(struct bcmv3d_mmu4_backing *b,
    unsigned int slot)
{
	struct mmu4_gem_slot *g;

	if (b == NULL || slot >= BCM2712_MMU4_SCANOUT_SLOTS)
		return EINVAL;
	if (cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	ASSERT_SLEEPABLE();
	if (b->running || b->range_fault != 0)
		return EBUSY;
	g = b->staged[slot];
	if (g == NULL || !g->linked || g->published ||
	    (g->exposed && !g->retired) ||
	    g->bo == NULL)
		return EBUSY;
	mmu4_gem_drop_bo(b, g);
	return 0;
}

void
bcmv3d_mmu4_backing_range_abort(struct bcmv3d_mmu4_backing *b, int error)
{

	if (b != NULL && error > 0 && b->range_fault == 0)
		b->range_fault = error;
}

int
bcmv3d_mmu4_backing_prepare_gem_read(struct bcmv3d_mmu4_backing *b,
    unsigned int slot, struct drm_gem_object *obj)
{
	struct mmu4_gem_slot *g;
	int error;

	if (b == NULL || obj == NULL || slot >= BCM2712_MMU4_SCANOUT_SLOTS)
		return EINVAL;
	error = bcmv3d_mmu4_backing_range_check(b, slot,
	    BCMV3D_MMU4_RANGE_PUBLISH);
	if (error != 0)
		return error;
	g = b->staged[slot];
	if (&g->bo->cma.base != obj)
		return EXDEV;
	if (g->read_synced)
		return EALREADY;
	/* The lifecycle owner has already waited and published its read fence. */
	bus_dmamap_sync(b->sc->dmat, g->map, 0, g->bo->cma.dmasize,
	    BUS_DMASYNC_PREWRITE);
	g->read_synced = true;
	return 0;
}

int
bcmv3d_mmu4_backing_expose_gem(struct bcmv3d_mmu4_backing *b,
    unsigned int slot, struct drm_gem_object *obj)
{
	struct mmu4_gem_slot *g;
	int error;

	if (b == NULL || obj == NULL || slot >= BCM2712_MMU4_SCANOUT_SLOTS)
		return EINVAL;
	/* Revalidate the actual mapped BO and tables before the first HVS write. */
	error = bcmv3d_mmu4_backing_range_check(b, slot,
	    BCMV3D_MMU4_RANGE_UNMAP);
	if (error != 0)
		return error;
	g = b->staged[slot];
	if (&g->bo->cma.base != obj)
		return EXDEV;
	if (!g->read_synced)
		return EBUSY;
	/* Keep exposure sticky. Only the retained worker proof can authorize unmap. */
	g->exposed = true;
	return 0;
}

#ifdef WSDISPLAY_BOOTFB_HANDOFF
int
bcmv3d_mmu4_backing_authorize_retired(struct bcmv3d_mmu4_backing *b,
    unsigned int slot, struct bcmv3d_scanout_frame *frame)
{
	struct mmu4_gem_slot *g;
	int error;

	if (b == NULL || frame == NULL || slot >= BCM2712_MMU4_SCANOUT_SLOTS)
		return EINVAL;
	if (cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	ASSERT_SLEEPABLE();
	if (b->range_fault != 0)
		return b->range_fault;
	g = b->staged[slot];
	if (b->running || g == NULL || g->bo == NULL || !g->linked ||
	    !g->published || !g->read_synced || !g->exposed || g->retired)
		return EBUSY;
	error = bcmv3d_scanout_retirement_check(frame, b->sc->mmu4_native,
	    slot, &g->bo->cma.base);
	if (error == 0)
		g->retired = true;
	return error;
}
#endif
