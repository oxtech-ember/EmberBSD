/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD native DMA/GEM buffer ownership and UVM mapping.
 * UAPI semantics: MIT Linux PowerVR 7b63ef2d55f24519e7e9e5f4d15dbea03f126e40.
 * The fault pager derives from the native CMA pager and adds cacheable maps.
 */

#include <sys/cdefs.h>
#include <sys/atomic.h>
#include <sys/param.h>
#include <sys/file.h>
#include <sys/kmem.h>
#include <sys/mman.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <linux/err.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_prime.h>
#include <drm/drm_vma_manager.h>
#include <linux/dma-buf.h>
#include <uvm/uvm.h>
#include <uvm/uvm_extern.h>
#include <uvm/uvm_pdaemon.h>
#include <dev/powervr/pvr_mmu.h>
#include "pvr_backend.h"
#include "pvr_drm.h"
#include "pvr_drv.h"

#define PVR_DEVICE_PAGE_SIZE	4096

struct pvr_bo {
	/* The native CMA pager reaches this member from the GEM object. */
	struct drm_gem_cma_object cma;
	bus_dma_segment_t *segments;
	uint64_t *segment_ends;
	size_t segment_bytes;
	int nsegments, segment_capacity;
	uint64_t flags;
	int dma_owned;
	bool coherent;
	struct dma_buf *import_buf;	/* pages belong to this dma-buf */
	bool firmware_pinned;
	unsigned int firmware_holds;
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

	KASSERT(!bo->firmware_pinned && bo->firmware_holds == 0);
	drm_gem_object_release(obj);
	/* Retire CPU cache lines of a cached BO before its pages are reused. */
	if ((bo->flags & DRM_PVR_BO_EMBER_CPU_CACHED) != 0) {
		bus_dmamap_sync(cma->dmat, cma->dmamap, 0, cma->dmasize,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
		bo->dma_owned |= BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE;
	}
	if (bo->dma_owned != 0)
		bus_dmamap_sync(cma->dmat, cma->dmamap, 0, cma->dmasize,
		    ((bo->dma_owned & BUS_DMASYNC_PREWRITE) != 0 ?
		    BUS_DMASYNC_POSTWRITE : 0) |
		    ((bo->dma_owned & BUS_DMASYNC_PREREAD) != 0 ?
		    BUS_DMASYNC_POSTREAD : 0));
	bus_dmamap_unload(cma->dmat, cma->dmamap);
	bus_dmamap_destroy(cma->dmat, cma->dmamap);
	if (cma->vaddr != NULL)
		bus_dmamem_unmap(cma->dmat, cma->vaddr, cma->dmasize);
	if (bo->import_buf != NULL)
		dma_buf_put(bo->import_buf);
	else
		bus_dmamem_free(cma->dmat, bo->segments, bo->nsegments);
	if (bo->segments != cma->dmasegs)
		kmem_free(bo->segments, bo->segment_bytes);
	kmem_free(bo, sizeof(*bo));
	/* CPU mappings can outlive both their GEM handle and their DRM fd. */
	drm_dev_put(dev);
}

static struct dma_buf *pvr_bo_export(struct drm_gem_object *, int);
static struct sg_table *pvr_bo_get_sg_table(struct drm_gem_object *);

static const struct drm_gem_object_funcs pvr_bo_funcs = {
	.export = pvr_bo_export,
	.get_sg_table = pvr_bo_get_sg_table,
	.free = pvr_bo_free,
};

/* DMA maps can merge adjacent allocation segments. Validate both streams. */
static int
pvr_bo_validate(struct pvr_bo *bo)
{
	bus_dmamap_t map = bo->cma.dmamap;
	const uint64_t limit = UINT64_C(1) << PVR_MMU_PA_BITS;
	uint64_t total = 0, left, raw_offset = 0, dma_offset = 0, step;
	unsigned int i = 0, j = 0;

	if ((map->_dm_flags & _BUS_DMAMAP_IS_BOUNCING) != 0 ||
	    map->dm_nsegs == 0 || map->dm_nsegs > (unsigned)bo->nsegments ||
	    map->dm_mapsize < bo->cma.base.size)
		return EOPNOTSUPP;
	for (i = 0; i < map->dm_nsegs; i++) {
		if (map->dm_segs[i].ds_len > UINT64_MAX - total)
			return EINVAL;
		total += map->dm_segs[i].ds_len;
	}
	if (total < bo->cma.base.size)
		return EOPNOTSUPP;
	for (i = 0; i < map->dm_nsegs; i++) {
		uint64_t pa = map->dm_segs[i].ds_addr;
		uint64_t size = map->dm_segs[i].ds_len;
		if (size == 0 || ((pa | size) & (PVR_MMU_PAGE_SIZE - 1)) != 0 ||
		    pa >= limit || size > limit - pa)
			return EINVAL;
	}
	if (pvr_device_bvnc(bo->cma.base.dev) != UINT64_C(0x0016006600360026))
		return 0;
	/* The direct-DMA profile must retain physical identity for every byte. */
	i = 0;
	for (left = bo->cma.base.size; left != 0; left -= step) {
		if (i >= map->dm_nsegs || j >= (unsigned)bo->nsegments ||
		    raw_offset >= bo->segments[j].ds_len ||
		    map->dm_segs[i].ds_addr + dma_offset !=
		    bo->segments[j].ds_addr + raw_offset)
			return EOPNOTSUPP;
		step = MIN(left, MIN(map->dm_segs[i].ds_len - dma_offset,
		    bo->segments[j].ds_len - raw_offset));
		dma_offset += step;
		raw_offset += step;
		if (dma_offset == map->dm_segs[i].ds_len) {
			i++;
			dma_offset = 0;
		}
		if (raw_offset == bo->segments[j].ds_len) {
			j++;
			raw_offset = 0;
		}
	}
	return 0;
}

static uint64_t
pvr_bo_pte_flags(const struct pvr_bo *bo)
{
	uint64_t flags = 0;

	if ((bo->flags & DRM_PVR_BO_BYPASS_DEVICE_CACHE) != 0)
		flags |= PVR_MMU_PTE_SLC_BYPASS;
	if ((bo->flags & DRM_PVR_BO_PM_FW_PROTECT) != 0)
		flags |= PVR_MMU_PTE_PM_FW_PROTECT;
	return flags;
}

int
pvr_bo_mapping(struct drm_gem_object *obj, uint64_t offset, uint64_t size,
    uint64_t *address, uint64_t *flags)
{
	struct pvr_bo *bo;
	bus_dmamap_t map;
	unsigned int i;
	int error;

	if (obj == NULL || obj->funcs != &pvr_bo_funcs ||
	    address == NULL || flags == NULL || size == 0 ||
	    ((offset | size) & (PAGE_SIZE - 1)) != 0 ||
	    offset > obj->size || size > obj->size - offset)
		return EINVAL;
	bo = pvr_to_bo(obj);
	error = pvr_bo_validate(bo);
	if (error != 0)
		return error;
	map = bo->cma.dmamap;
	for (i = 0; i < map->dm_nsegs; i++) {
		if (offset >= map->dm_segs[i].ds_len) {
			offset -= map->dm_segs[i].ds_len;
			continue;
		}
		if (size > map->dm_segs[i].ds_len - offset)
			return EFBIG;
		*address = map->dm_segs[i].ds_addr + offset;
		*flags = pvr_bo_pte_flags(bo);
		return 0;
	}
	return EINVAL;
}

/* The caller supplies one output entry per 4 KiB GPU page. */
int
pvr_bo_pages(struct drm_gem_object *obj, uint64_t offset, uint64_t size,
    uint64_t *pages, uint64_t *flags)
{
	struct pvr_bo *bo;
	bus_dmamap_t map;
	uint64_t left, step, at = 0;
	unsigned int i;
	int error;

	if (obj == NULL || obj->funcs != &pvr_bo_funcs || pages == NULL ||
	    flags == NULL || size == 0 ||
	    ((offset | size) & (PAGE_SIZE - 1)) != 0 ||
	    offset > obj->size || size > obj->size - offset)
		return EINVAL;
	bo = pvr_to_bo(obj);
	error = pvr_bo_validate(bo);
	if (error != 0)
		return error;
	map = bo->cma.dmamap;
	left = size;
	for (i = 0; i < map->dm_nsegs && left != 0; i++) {
		if (offset >= map->dm_segs[i].ds_len) {
			offset -= map->dm_segs[i].ds_len;
			continue;
		}
		step = MIN(left, map->dm_segs[i].ds_len - offset);
		for (uint64_t n = 0; n < step; n += PVR_MMU_PAGE_SIZE)
			pages[at++] = map->dm_segs[i].ds_addr + offset + n;
		left -= step;
		offset = 0;
	}
	KASSERT(left == 0);
	*flags = pvr_bo_pte_flags(bo);
	return 0;
}

/* PRIME does not carry the cache attributes of this native extension. */
static struct dma_buf *
pvr_bo_export(struct drm_gem_object *obj, int flags)
{

	/* An import has no page list; its dma-buf is the exporter's. */
	if (pvr_to_bo(obj)->import_buf != NULL ||
	    (pvr_to_bo(obj)->flags & (DRM_PVR_BO_PM_FW_PROTECT |
	    DRM_PVR_BO_EMBER_CPU_CACHED)) != 0)
		return ERR_PTR(-EACCES);
	return drm_gem_prime_export(obj, flags);
}

static struct sg_table *
pvr_bo_get_sg_table(struct drm_gem_object *obj)
{
	struct pvr_bo *bo = pvr_to_bo(obj);
	int error;

	/* An exported page list must describe the original, complete BO. */
	if (bo->import_buf != NULL)
		return ERR_PTR(-EACCES);
	error = pvr_bo_validate(bo);
	if (error != 0)
		return ERR_PTR(-error);
	if ((pvr_to_bo(obj)->flags & (DRM_PVR_BO_PM_FW_PROTECT |
	    DRM_PVR_BO_EMBER_CPU_CACHED)) != 0)
		return ERR_PTR(-EACCES);
	return drm_prime_bus_dmamem_to_sg(bo->cma.dmat, bo->segments, bo->nsegments);
}

/* A PRIME fd authorizes this BO; its offset is relative, in bytes. */
int
pvr_prime_mmap(struct drm_gem_object *obj, off_t *offp, size_t size,
    int prot, int *flagsp, int *advicep, struct uvm_object **uobjp,
    int *maxprotp)
{

	if (*offp < 0 || (*offp & (PAGE_SIZE - 1)) != 0 || size == 0 ||
	    (size & (PAGE_SIZE - 1)) != 0 || (uint64_t)*offp > obj->size ||
	    size > obj->size - (size_t)*offp ||
	    (prot & ~(PROT_READ | PROT_WRITE)) != 0 ||
	    (*flagsp & MAP_SHARED) == 0 || (*flagsp & MAP_PRIVATE) != 0)
		return -EINVAL;
	if ((pvr_to_bo(obj)->flags & DRM_PVR_BO_ALLOW_CPU_USERSPACE_ACCESS) == 0 ||
	    (pvr_to_bo(obj)->flags & DRM_PVR_BO_PM_FW_PROTECT) != 0)
		return -EACCES;
	/* The native CMA pager drops this reference after the last mapping. */
	drm_gem_object_get(obj);
	*uobjp = &obj->gemo_uvmobj;
	*maxprotp = PROT_READ | PROT_WRITE;
	return 0;
}

static int
pvr_bo_segment(const struct pvr_bo *bo, off_t *offset)
{
	unsigned int low = 0, high;

	if (*offset < 0 || (uint64_t)*offset >= bo->cma.dmasize ||
	    bo->nsegments <= 0)
		return -1;
	if (bo->nsegments == 1)
		return 0;
	high = bo->nsegments - 1;
	while (low < high) {
		unsigned int middle = low + (high - low) / 2;
		if ((uint64_t)*offset < bo->segment_ends[middle])
			high = middle;
		else
			low = middle + 1;
	}
	if (low != 0)
		*offset -= bo->segment_ends[low - 1];
	return low;
}

static int
pvr_bo_fault(struct uvm_faultinfo *ufi, vaddr_t vaddr, struct vm_page **pps,
    int npages, int centeridx, vm_prot_t access_type, int flags)
{
	struct vm_map_entry *entry = ufi->entry;
	struct uvm_object *uobj = entry->object.uvm_obj;
	struct drm_gem_object *gem_obj =
	    container_of(uobj, struct drm_gem_object, gemo_uvmobj);
	struct drm_gem_cma_object *obj = to_drm_gem_cma_obj(gem_obj);
	off_t curr_offset;
	vaddr_t curr_va;
	paddr_t paddr, mdpgno;
	u_int mmapflags;
	int lcv, retval, dmaflags;
	vm_prot_t mapprot;

	if (UVM_ET_ISCOPYONWRITE(entry))
		return EIO;
	dmaflags = BUS_DMA_PREFETCHABLE;
	if ((pvr_to_bo(gem_obj)->flags & DRM_PVR_BO_EMBER_CPU_CACHED) != 0)
		dmaflags = 0;
	curr_offset = entry->offset + (vaddr - entry->start);
	curr_va = vaddr;
	retval = 0;
	for (lcv = 0; lcv < npages; lcv++, curr_offset += PAGE_SIZE,
	    curr_va += PAGE_SIZE) {
		off_t segment_offset = curr_offset;
		int segment;
		if ((flags & PGO_ALLPAGES) == 0 && lcv != centeridx)
			continue;
		if (pps[lcv] == PGO_DONTCARE)
			continue;
		segment = pvr_bo_segment(pvr_to_bo(gem_obj), &segment_offset);
		if (segment < 0) {
			retval = EIO;
			break;
		}
		mdpgno = bus_dmamem_mmap(obj->dmat,
		    &pvr_to_bo(gem_obj)->segments[segment], 1,
		    segment_offset, access_type, dmaflags);
		if (mdpgno == (paddr_t)-1) {
			retval = EIO;
			break;
		}
		paddr = pmap_phys_address(mdpgno);
		mmapflags = pmap_mmap_flags(mdpgno);
		mapprot = ufi->entry->protection;
		if (pmap_enter(ufi->orig_map->pmap, curr_va, paddr, mapprot,
		    PMAP_CANFAIL | mapprot | mmapflags) != 0) {
			pmap_update(ufi->orig_map->pmap);
			uvmfault_unlockall(ufi, ufi->entry->aref.ar_amap, uobj);
			return ENOMEM;
		}
	}
	pmap_update(ufi->orig_map->pmap);
	uvmfault_unlockall(ufi, ufi->entry->aref.ar_amap, uobj);
	return retval;
}

const struct uvm_pagerops pvr_bo_uvm_ops = {
	.pgo_reference = drm_gem_pager_reference,
	.pgo_detach = drm_gem_pager_detach,
	.pgo_fault = pvr_bo_fault,
};

static void
pvr_bo_report_failure(const char *stage, int error, size_t size, uint64_t flags)
{

	printf("PVR_BO_CREATE_FAILED stage=%s error=%d size=%zu flags=%#jx\n",
	    stage, error, size, (uintmax_t)flags);
}

/*
 * NOWAIT page allocation counts only free pages, while NetBSD keeps most idle
 * RAM as reclaimable file cache. A WAITOK allocation may wait for the
 * pagedaemon without bound. Allocate scattered backing with NOWAIT calls
 * instead, each taking what the free margin allows: free pages stay at most
 * PVR_BO_MARGIN_PAGES below freemin, so other processes keep their margin and
 * a short supply always wakes the pagedaemon; otherwise sleep one tick.
 * Each call pauses and resumes the per-CPU free page caches (a cross-call to
 * every CPU), so calls must be few: with 64-page calls a 128 MB BO in
 * fragmented memory took 1.3 s. A call holds the free list locks while it
 * collects its pages, so it takes at most PVR_BO_CALL_PAGES.
 * Give up after a bounded total time. An allocation failure with enough free
 * pages is not a cache shortage: retry it once, then fail. No gate is held.
 */
#define PVR_BO_MARGIN_PAGES	64
#define PVR_BO_CALL_PAGES	2048
#define PVR_BO_RECLAIM_SECONDS	2

static int
pvr_bo_alloc_pages(bus_dma_tag_t dmat, size_t size, bus_dma_segment_t *segs,
    int capacity, int *nsegsp, unsigned int *waitsp)
{
	const int reserve = uvmexp.reserve_pagedaemon + uvmexp.reserve_kernel;
	const int floor = MAX(reserve, uvmexp.freemin - PVR_BO_MARGIN_PAGES - 1);
	struct timespec now, deadline;
	size_t done = 0, chunk;
	unsigned int waits = 0;
	int nsegs = 0, n, error = 0, paused;
	bool retried = false;

	getnanouptime(&deadline);
	deadline.tv_sec += PVR_BO_RECLAIM_SECONDS;
	while (done < size) {
		const size_t want = MIN(size - done,
		    (size_t)PVR_BO_CALL_PAGES * PAGE_SIZE);
		const int room = uvm_availmem(false) - floor - 1;

		/* Wait for a useful amount, not for whatever single pages the
		 * pagedaemon has freed so far. */
		chunk = MIN(want, (size_t)MAX(room, 0) * PAGE_SIZE);
		if (chunk >= MIN(want, (size_t)PVR_BO_MARGIN_PAGES * PAGE_SIZE)) {
			n = 0;
			error = bus_dmamem_alloc(dmat, chunk, PAGE_SIZE, 0,
			    segs + nsegs, capacity - nsegs, &n, BUS_DMA_NOWAIT);
			if (error == 0) {
				nsegs += n;
				done += chunk;
				continue;
			}
			if (error != ENOMEM || retried)
				break;
			retried = true;
		}
		error = ENOMEM;
		getnanouptime(&now);
		if (timespeccmp(&now, &deadline, >=) || !uvm_reclaimable())
			break;
		uvm_kick_pdaemon();
		paused = kpause("pvrbomem", true, 1, NULL);
		waits++;
		if (paused != 0 && paused != EWOULDBLOCK) {
			error = EINTR;
			break;
		}
	}
	*waitsp = waits;
	if (error != 0) {
		if (nsegs != 0)
			bus_dmamem_free(dmat, segs, nsegs);
		return error;
	}
	*nsegsp = nsegs;
	return 0;
}

static struct pvr_bo *
pvr_bo_alloc(struct drm_device *dev, size_t size, uint64_t flags)
{
	struct pvr_bo *bo;
	struct drm_gem_cma_object *cma;
	const char *stage = "dmamem_alloc";
	static volatile unsigned int sg_events, reclaim_events;
	unsigned int waits = 0;
	int error, nsegs = 0;

	bo = kmem_zalloc(sizeof(*bo), KM_SLEEP);
	cma = &bo->cma;
	cma->dmat = dev->dmat;
	cma->dmasize = size;
	bo->flags = flags;
	bo->segments = cma->dmasegs;
	bo->segment_capacity = 1;
	error = bus_dmamem_alloc(cma->dmat, size, PAGE_SIZE, 0,
	    cma->dmasegs, 1, &nsegs, BUS_DMA_NOWAIT);
	/* GPU-only images also use page-table mappings. Keep protected firmware
	 * allocations contiguous; CPU access permission is not a DMA constraint. */
	if (error == ENOMEM && size > PAGE_SIZE &&
	    (flags & DRM_PVR_BO_PM_FW_PROTECT) == 0) {
		size_t pages = size / PAGE_SIZE;

		/* Wired memory and the free target cannot be reclaimed for it. */
		if (pages > (size_t)uvmexp.npages ||
		    (uint64_t)pages + (uint64_t)uvmexp.wired +
		    (uint64_t)uvmexp.freetarg > (uint64_t)uvmexp.npages)
			goto free_bo;
		bo->segment_capacity = size / PAGE_SIZE;
		bo->segment_bytes = pages *
		    (sizeof(*bo->segments) + sizeof(*bo->segment_ends));
		bo->segments = kmem_zalloc(bo->segment_bytes, KM_NOSLEEP);
		if (bo->segments == NULL) {
			bo->segments = cma->dmasegs;
			goto free_bo;
		}
		error = pvr_bo_alloc_pages(cma->dmat, size, bo->segments,
		    bo->segment_capacity, &nsegs, &waits);
		if (error == 0) {
			uint64_t end = 0;
			bo->segment_ends = (uint64_t *)(bo->segments + bo->segment_capacity);
			for (int n = 0; n < nsegs; n++) {
				end += bo->segments[n].ds_len;
				bo->segment_ends[n] = end;
			}
		}
		/* Bounded diagnostics: first plain fallbacks, first reclaims. */
		if (waits != 0 ? atomic_inc_uint_nv(&reclaim_events) <= 16 :
		    error == 0 && atomic_inc_uint_nv(&sg_events) <= 4)
			printf("PVR_BO_SG_FALLBACK size=%zu segments=%d waits=%u "
			    "error=%d free=%d\n", size, nsegs, waits, error,
			    uvm_availmem(false));
	}
	if (error != 0)
		goto free_bo;
	bo->nsegments = nsegs;
	/*
	 * Use the same CPU memory attributes as the user mapping. A cached
	 * BO is then cleaned by the ordinary non-coherent DMA syncs below.
	 */
	stage = "dmamem_map";
	error = bus_dmamem_map(cma->dmat, bo->segments, nsegs, size,
	    &cma->vaddr, BUS_DMA_NOWAIT |
	    ((flags & DRM_PVR_BO_EMBER_CPU_CACHED) != 0 ?
	    0 : BUS_DMA_PREFETCHABLE));
	if (error != 0)
		goto free_dma;
	stage = "dmamap_create";
	error = bus_dmamap_create(cma->dmat, size, nsegs, size, 0,
	    BUS_DMA_NOWAIT, &cma->dmamap);
	if (error != 0)
		goto unmap;
	stage = "dmamap_load";
	error = bus_dmamap_load(cma->dmat, cma->dmamap, cma->vaddr,
	    size, NULL, BUS_DMA_NOWAIT);
	if (error != 0)
		goto destroy;
	memset(cma->vaddr, 0, size);
	bus_dmamap_sync(cma->dmat, cma->dmamap, 0, size,
	    BUS_DMASYNC_PREWRITE);
	bo->dma_owned = BUS_DMASYNC_PREWRITE;
	/* Syncs of a coherent, non-bouncing map are only memory barriers. */
	bo->coherent = (cma->dmamap->_dm_flags &
	    (_BUS_DMAMAP_COHERENT | _BUS_DMAMAP_IS_BOUNCING)) ==
	    _BUS_DMAMAP_COHERENT;
	drm_dev_get(dev);
	drm_gem_private_object_init(dev, &cma->base, size);
	cma->base.funcs = &pvr_bo_funcs;
	return bo;

destroy:
	bus_dmamap_destroy(cma->dmat, cma->dmamap);
unmap:
	bus_dmamem_unmap(cma->dmat, cma->vaddr, size);
free_dma:
	bus_dmamem_free(cma->dmat, bo->segments, nsegs);
free_bo:
	if (bo->segments != cma->dmasegs)
		kmem_free(bo->segments, bo->segment_bytes);
	pvr_bo_report_failure(stage, error, size, flags);
	kmem_free(bo, sizeof(*bo));
	return ERR_PTR(-error);
}

/*
 * Import a dma-buf exported by another DRM device from a CMA object, such
 * as a display scanout buffer, so that the GPU can render or copy into it.
 * The buffer may live in memory reserved before UVM, outside the kernel
 * direct map and without vm_page, so it cannot travel as a page list and
 * generic CPU mapping or cache maintenance would fault on it. The BO takes
 * the exporter's physical segment for the GPU MMU only: no kernel mapping,
 * no CPU access, no cache maintenance. It holds the dma-buf, and with it
 * the exporter's object, until it is freed.
 */
struct drm_gem_object *
pvr_gem_prime_import(struct drm_device *dev, struct dma_buf *dma_buf)
{
	struct drm_gem_object *src;
	struct drm_gem_cma_object *from, *cma;
	struct pvr_bo *bo;
	size_t size;
	int error;

	if (dma_buf->ops->release != drm_gem_dmabuf_release)
		return ERR_PTR(-EINVAL);
	src = dma_buf->priv;
	if (src->dev == dev) {
		/* Our own export: hand back the original object. */
		drm_gem_object_get(src);
		return src;
	}
	if (src->dev->driver->gem_free_object != drm_gem_cma_free_object)
		return ERR_PTR(-EINVAL);
	from = to_drm_gem_cma_obj(src);
	size = src->size;
	if (size == 0 || (size & (PAGE_SIZE - 1)) != 0 ||
	    from->dmasegs[0].ds_len < size)
		return ERR_PTR(-EINVAL);
	bo = kmem_zalloc(sizeof(*bo), KM_SLEEP);
	cma = &bo->cma;
	cma->dmat = dev->dmat;
	cma->dmasize = size;
	cma->dmasegs[0].ds_addr = from->dmasegs[0].ds_addr;
	cma->dmasegs[0].ds_len = size;
	bo->segments = cma->dmasegs;
	bo->segment_capacity = 1;
	bo->nsegments = 1;
	bo->import_buf = dma_buf;
	error = bus_dmamap_create(cma->dmat, size, 1, size, 0, BUS_DMA_WAITOK,
	    &cma->dmamap);
	if (error != 0)
		goto free_bo;
	error = bus_dmamap_load_raw(cma->dmat, cma->dmamap, bo->segments, 1,
	    size, BUS_DMA_WAITOK);
	if (error != 0)
		goto destroy;
	/* No CPU mapping exists, so there are no cache lines to maintain. */
	bo->coherent = true;
	get_dma_buf(dma_buf);
	drm_dev_get(dev);
	drm_gem_private_object_init(dev, &cma->base, size);
	cma->base.funcs = &pvr_bo_funcs;
	return &cma->base;

destroy:
	bus_dmamap_destroy(cma->dmat, cma->dmamap);
free_bo:
	pvr_bo_report_failure("prime_import", error, size, 0);
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
	const uint64_t cached = DRM_PVR_BO_EMBER_CPU_CACHED |
	    DRM_PVR_BO_ALLOW_CPU_USERSPACE_ACCESS;
	size_t size;
	uint32_t handle;
	int error;

	if (args->_padding_c != 0 || args->size == 0 ||
	    args->size > SIZE_MAX - (PAGE_SIZE - 1) ||
	    (args->size & (PVR_DEVICE_PAGE_SIZE - 1)) != 0 ||
	    (args->flags & ~DRM_PVR_BO_FLAGS_MASK) != 0 ||
	    (args->flags & conflict) == conflict ||
	    ((args->flags & DRM_PVR_BO_EMBER_CPU_CACHED) != 0 &&
	    (args->flags & cached) != cached))
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
	else
		pvr_bo_report_failure("gem_handle", -error, size, args->flags);
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
	/* Kernel users assume the coherent write-combined attributes. */
	if (bo->import_buf != NULL || (bo->flags & flags) != flags ||
	    (bo->flags & DRM_PVR_BO_EMBER_CPU_CACHED) != 0)
		return EACCES;
	*cpu = (uint8_t *)bo->cma.vaddr + offset;
	return 0;
}

bool
pvr_bo_coherent(struct drm_gem_object *obj)
{

	KASSERT(obj->funcs == &pvr_bo_funcs);
	return pvr_to_bo(obj)->coherent;
}

/* One barrier serves every coherent BO; no cache or ownership changes. */
void
pvr_bo_barrier(struct drm_gem_object *obj, int operations)
{
	struct pvr_bo *bo = pvr_to_bo(obj);

	KASSERT(obj->funcs == &pvr_bo_funcs && bo->coherent);
	/* An import has no CPU mapping; its pages may lack a direct map. */
	if (bo->import_buf != NULL)
		return;
	bus_dmamap_sync(bo->cma.dmat, bo->cma.dmamap, 0, bo->cma.dmasize,
	    operations);
}

bool
pvr_bo_needs_sync(struct drm_gem_object *obj)
{
	struct pvr_bo *bo = pvr_to_bo(obj);

	KASSERT(obj->funcs == &pvr_bo_funcs);
	return !bo->coherent && bo->import_buf == NULL &&
	    (bo->flags & DRM_PVR_BO_EMBER_CPU_CACHED) == 0;
}

bool
pvr_bo_imported(struct drm_gem_object *obj)
{

	KASSERT(obj->funcs == &pvr_bo_funcs);
	return pvr_to_bo(obj)->import_buf != NULL;
}

void
pvr_bo_sync(struct drm_gem_object *obj, int operations)
{
	struct pvr_bo *bo = pvr_to_bo(obj);
	int active = operations;

	KASSERT(obj->funcs == &pvr_bo_funcs);
	/* Userspace maintains a cached BO; per-job syncs would only cost time. */
	if ((bo->flags & DRM_PVR_BO_EMBER_CPU_CACHED) != 0 ||
	    bo->import_buf != NULL)
		return;
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
	if (bo->firmware_pinned || bo->firmware_holds != 0)
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

int
pvr_bo_hold(struct drm_gem_object *obj)
{
	struct pvr_bo *bo;

	if (obj == NULL || obj->funcs != &pvr_bo_funcs)
		return EINVAL;
	bo = pvr_to_bo(obj);
	if (bo->firmware_pinned)
		return EBUSY;
	if (bo->firmware_holds == UINT_MAX)
		return EOVERFLOW;
	bo->firmware_holds++;
	return 0;
}

void
pvr_bo_unhold(struct drm_gem_object *obj)
{
	struct pvr_bo *bo = pvr_to_bo(obj);

	KASSERT(obj->funcs == &pvr_bo_funcs && bo->firmware_holds != 0);
	bo->firmware_holds--;
}
