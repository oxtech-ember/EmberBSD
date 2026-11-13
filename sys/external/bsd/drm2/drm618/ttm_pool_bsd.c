/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD.  Written to the interface of
 * include/drm/ttm/ttm_pool.h and the way ttm_tt.c calls it (MIT files of
 * dist618), no Linux code; the structure follows the 5.6 replacement
 * ttm_bus_dma.c of this tree.
 *
 * The page pool of TTM on the 6.18 core.  Linux's ttm_pool.c is a page
 * allocator (alloc_pages, set_pages_wc, dma_map_page, a shrinker, memcg);
 * this one gives a ttm_tt its pages from uvm and the DMA addresses of them
 * from bus_dma:
 *
 *   - ttm_pool_alloc: uvm_pglistalloc of tt->num_pages pages (below 4 GiB for
 *     a use_dma32 pool), zeroed when TTM_TT_FLAG_ZERO_ALLOC asks, stored in
 *     tt->pages.  When tt->dma_address is set and the device has a bus_dma
 *     tag (drm618_ttm_set_dmat), the pages are loaded into a bus_dmamap and
 *     dma_address[i] comes from its segments; without a tag the bus address
 *     is the physical address (x86 and arm64 without an IOMMU), and the pool
 *     says so once.
 *   - ttm_pool_free: unload the map, free the pages.
 *   - The cache attribute (tt->caching: cached, write-combined, uncached) is
 *     NOT applied to the pages here (Linux changes the direct map): CPU
 *     mappings made by TTM carry the attribute (ttm_prot_from_caching, with
 *     the PMAP_* flags of drm618_mm.h), as on the 5.6 core.
 *   - A pool keeps no free pages (Linux caches freed pages per order): every
 *     free returns them to uvm.  No shrinker: nothing asks the pool to give
 *     memory back, and ttm_pool_backup (swap-out) moves pages to a shmem
 *     backup (ttm_backup_bsd.c).
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#include <sys/queue.h>
#include <sys/systm.h>

#include <uvm/uvm_extern.h>
#include <uvm/uvm_page.h>

#include <linux/mm.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include <drm/bus_dma_hacks.h>
#include <drm/ttm/ttm_backup.h>
#include <drm/ttm/ttm_pool.h>
#include <drm/ttm/ttm_tt.h>

/* ---- the DMA tag of a device and the maps of the tts --------------------- */

struct dmat_entry {
	LIST_ENTRY(dmat_entry)	link;
	const struct device	*dev;
	bus_dma_tag_t		dmat;
};

struct tt_map {
	LIST_ENTRY(tt_map)	link;
	const struct ttm_tt	*tt;
	bus_dma_tag_t		dmat;
	bus_dmamap_t		map;
};

static kmutex_t pool_lock;
static bool pool_lock_inited;
static LIST_HEAD(, dmat_entry) dmats = LIST_HEAD_INITIALIZER(dmats);
static LIST_HEAD(, tt_map) tt_maps = LIST_HEAD_INITIALIZER(tt_maps);
static volatile unsigned says_no_dmat;
static volatile unsigned long pages_out;	/* pages given out by ttm_pool_alloc and not yet freed */

unsigned long	drm618_ttm_pool_pages(void);

/* The number of pages the pools have handed out and not got back (a leak check for tests). */
unsigned long
drm618_ttm_pool_pages(void)
{

	return pages_out;
}

void	drm618_ttm_set_dmat(const struct device *, bus_dma_tag_t);

/* Names the bus_dma tag of a device for its pool (NULL: forget it). */
void
drm618_ttm_set_dmat(const struct device *dev, bus_dma_tag_t dmat)
{
	struct dmat_entry *e;

	mutex_enter(&pool_lock);
	LIST_FOREACH(e, &dmats, link) {
		if (e->dev == dev)
			break;
	}
	if (e != NULL && dmat == NULL) {
		LIST_REMOVE(e, link);
		mutex_exit(&pool_lock);
		kmem_free(e, sizeof(*e));
		return;
	}
	if (e == NULL && dmat != NULL) {
		mutex_exit(&pool_lock);
		e = kmem_alloc(sizeof(*e), KM_SLEEP);
		e->dev = dev;
		e->dmat = dmat;
		mutex_enter(&pool_lock);
		LIST_INSERT_HEAD(&dmats, e, link);
	} else if (e != NULL) {
		e->dmat = dmat;
	}
	mutex_exit(&pool_lock);
}

static bus_dma_tag_t
dmat_of(const struct device *dev)
{
	struct dmat_entry *e;
	bus_dma_tag_t t = NULL;

	mutex_enter(&pool_lock);
	LIST_FOREACH(e, &dmats, link) {
		if (e->dev == dev) {
			t = e->dmat;
			break;
		}
	}
	mutex_exit(&pool_lock);
	return t;
}

/* ---- the pool ----------------------------------------------------------------- */

int
ttm_pool_mgr_init(unsigned long num_pages __unused)
{

	if (!pool_lock_inited) {
		mutex_init(&pool_lock, MUTEX_DEFAULT, IPL_NONE);
		pool_lock_inited = true;
	}
	return 0;
}

void
ttm_pool_mgr_fini(void)
{

	if (pool_lock_inited) {
		KASSERT(LIST_EMPTY(&tt_maps));
		mutex_destroy(&pool_lock);
		pool_lock_inited = false;
	}
}

void
ttm_pool_init(struct ttm_pool *pool, struct device *dev, int nid,
    bool use_dma_alloc, bool use_dma32)
{

	pool->dev = dev;
	pool->nid = nid;
	pool->use_dma_alloc = use_dma_alloc;
	pool->use_dma32 = use_dma32;
}

void
ttm_pool_fini(struct ttm_pool *pool __unused)
{
}

static int
tt_map_load(struct ttm_pool *pool, struct ttm_tt *tt, bus_dma_tag_t dmat)
{
	struct tt_map *m;
	bus_size_t size = (bus_size_t)tt->num_pages << PAGE_SHIFT;
	bus_dmamap_t map;
	unsigned seg, i = 0;
	int error;

	error = bus_dmamap_create(dmat, size, tt->num_pages, size, 0,
	    BUS_DMA_WAITOK, &map);
	if (error) {
		printf("ttm: bus_dmamap_create of %u pages failed: %d\n", (unsigned)tt->num_pages, error);
		return -error;
	}
	error = bus_dmamap_load_pages(dmat, map, tt->pages, size, BUS_DMA_WAITOK);
	if (error) {
		if (error != EINVAL)
			printf("ttm: bus_dmamap_load_pages of %u pages failed: %d\n",
			    (unsigned)tt->num_pages, error);
		bus_dmamap_destroy(dmat, map);
		return -error;
	}
	for (seg = 0; seg < (unsigned)map->dm_nsegs; seg++) {
		bus_addr_t a = map->dm_segs[seg].ds_addr;
		bus_size_t l = map->dm_segs[seg].ds_len;

		for (; l >= PAGE_SIZE && i < tt->num_pages; l -= PAGE_SIZE) {
			tt->dma_address[i++] = a;
			a += PAGE_SIZE;
		}
	}
	KASSERTMSG(i == tt->num_pages, "dma map covers %u of %u pages", i,
	    (unsigned)tt->num_pages);
	m = kmem_alloc(sizeof(*m), KM_SLEEP);
	m->tt = tt;
	m->dmat = dmat;
	m->map = map;
	mutex_enter(&pool_lock);
	LIST_INSERT_HEAD(&tt_maps, m, link);
	mutex_exit(&pool_lock);
	return 0;
}

static void
tt_map_unload(const struct ttm_tt *tt)
{
	struct tt_map *m;

	mutex_enter(&pool_lock);
	LIST_FOREACH(m, &tt_maps, link) {
		if (m->tt == tt)
			break;
	}
	if (m != NULL)
		LIST_REMOVE(m, link);
	mutex_exit(&pool_lock);
	if (m == NULL)
		return;
	bus_dmamap_unload(m->dmat, m->map);
	bus_dmamap_destroy(m->dmat, m->map);
	kmem_free(m, sizeof(*m));
}

int
ttm_pool_alloc(struct ttm_pool *pool, struct ttm_tt *tt,
    struct ttm_operation_ctx *ctx __unused)
{
	struct pglist pgl;
	struct vm_page *pg;
	paddr_t high = pool->use_dma32 ? 0xffffffffUL : (paddr_t)-1;
	bus_dma_tag_t dmat;
	unsigned i;
	int error;

	KASSERT(tt->num_pages != 0);
retry:
	i = 0;
	TAILQ_INIT(&pgl);
	error = uvm_pglistalloc((psize_t)tt->num_pages << PAGE_SHIFT, 0, high,
	    PAGE_SIZE, 0, &pgl, tt->num_pages, 1);
	if (error)
		return -error;
	TAILQ_FOREACH(pg, &pgl, pageq.queue) {
		struct page *page = container_of(pg, struct page, p_vmp);

		if (tt->page_flags & TTM_TT_FLAG_ZERO_ALLOC)
			clear_highpage(page);
		tt->pages[i++] = page;
	}
	KASSERT(i == tt->num_pages);
	atomic_add_long(&pages_out, tt->num_pages);
	/* The pages are on tt->pages now: pgl is not a list of the pages any more. */
	TAILQ_INIT(&pgl);

	if (tt->dma_address != NULL) {
		dmat = dmat_of(pool->dev);
		if (dmat != NULL) {
			error = tt_map_load(pool, tt, dmat);
			if (error == -EINVAL && high != 0xffffffffUL) {
				/*
				 * A page is out of reach of the tag (x86: above its
				 * bounce threshold; the load does not bounce): the
				 * device addresses 32 bits, take the pages below 4 GiB.
				 */
				ttm_pool_free(pool, tt);
				high = 0xffffffffUL;
				goto retry;
			}
			if (error) {
				ttm_pool_free(pool, tt);
				return error;
			}
		} else {
			if (atomic_swap_uint(&says_no_dmat, 1) == 0)
				printf("ttm: no bus_dma tag for the device, "
				    "bus address = physical address\n");
			for (i = 0; i < tt->num_pages; i++)
				tt->dma_address[i] =
				    VM_PAGE_TO_PHYS(&tt->pages[i]->p_vmp);
		}
	}
	return 0;
}

void
ttm_pool_free(struct ttm_pool *pool __unused, struct ttm_tt *tt)
{
	struct pglist pgl;
	unsigned i;

	tt_map_unload(tt);
	TAILQ_INIT(&pgl);
	for (i = 0; i < tt->num_pages; i++) {
		struct page *page = tt->pages[i];

		/* A page that was backed up is a handle, not a page. */
		if (page == NULL || ttm_backup_page_ptr_is_handle(page))
			continue;
		TAILQ_INSERT_TAIL(&pgl, &page->p_vmp, pageq.queue);
		tt->pages[i] = NULL;
		if (tt->dma_address != NULL)
			tt->dma_address[i] = 0;
		atomic_dec_ulong(&pages_out);
	}
	uvm_pglistfree(&pgl);
}

int
ttm_pool_debugfs(struct ttm_pool *pool __unused, struct seq_file *m __unused)
{

	return 0;
}

/* ---- swap-out to a backup ---------------------------------------------------- */

void
ttm_pool_drop_backed_up(struct ttm_tt *tt)
{
	unsigned i;

	if (tt->backup == NULL)
		return;
	for (i = 0; i < tt->num_pages; i++) {
		struct page *page = tt->pages[i];

		if (page != NULL && ttm_backup_page_ptr_is_handle(page)) {
			ttm_backup_drop(tt->backup,
			    ttm_backup_page_ptr_to_handle(page));
			tt->pages[i] = NULL;
		}
	}
}

long
ttm_pool_backup(struct ttm_pool *pool, struct ttm_tt *tt,
    const struct ttm_backup_flags *flags)
{
	struct pglist pgl;
	unsigned i, done = 0;
	s64 handle;

	if (tt->backup == NULL)
		return 0;
	if (flags->purge) {
		/* the contents are not needed: just give the pages back */
		ttm_pool_free(pool, tt);
		return tt->num_pages;
	}
	/* Back up page by page; a failure keeps the rest of the pages. */
	TAILQ_INIT(&pgl);
	for (i = 0; i < tt->num_pages; i++) {
		struct page *page = tt->pages[i];

		if (page == NULL || ttm_backup_page_ptr_is_handle(page))
			continue;
		handle = ttm_backup_backup_page(tt->backup, page,
		    flags->writeback, i, GFP_KERNEL, GFP_KERNEL);
		if (handle < 0)
			break;
		tt->pages[i] = ttm_backup_handle_to_page_ptr(handle);
		TAILQ_INSERT_TAIL(&pgl, &page->p_vmp, pageq.queue);
		done++;
		atomic_dec_ulong(&pages_out);
	}
	if (done != 0) {
		tt_map_unload(tt);
		uvm_pglistfree(&pgl);
	}
	if (done != 0 && done != tt->num_pages && i < tt->num_pages) {
		/* partial: bring the tt back to a whole state is the caller's retry */
	}
	return done;
}

int
ttm_pool_restore_and_alloc(struct ttm_pool *pool, struct ttm_tt *tt,
    const struct ttm_operation_ctx *ctx)
{
	struct page **saved;
	unsigned i;
	int error;

	/* Take the handles aside, allocate the pages, copy the contents back. */
	saved = kmem_alloc(sizeof(*saved) * tt->num_pages, KM_SLEEP);
	for (i = 0; i < tt->num_pages; i++) {
		saved[i] = tt->pages[i];
		tt->pages[i] = NULL;
	}
	error = ttm_pool_alloc(pool, tt, (struct ttm_operation_ctx *)__UNCONST(ctx));
	if (error == 0) {
		for (i = 0; i < tt->num_pages && error == 0; i++) {
			if (saved[i] != NULL && ttm_backup_page_ptr_is_handle(saved[i])) {
				error = ttm_backup_copy_page(tt->backup, tt->pages[i],
				    ttm_backup_page_ptr_to_handle(saved[i]), false);
				if (error == 0)
					ttm_backup_drop(tt->backup,
					    ttm_backup_page_ptr_to_handle(saved[i]));
			}
		}
		if (error)
			ttm_pool_free(pool, tt);
	}
	if (error)
		for (i = 0; i < tt->num_pages; i++)
			tt->pages[i] = saved[i];
	kmem_free(saved, sizeof(*saved) * tt->num_pages);
	return error;
}
