/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD; written to the interface the
 * TTM of Linux 6.18 uses (ttm_tt.c, ttm_bo_util.c, ttm_resource.c,
 * ttm_device.c), no Linux code.
 *
 * Page and mapping model for TTM on the 6.18 core (read from drm618_mm.h):
 *
 *   - struct page is NetBSD's struct vm_page (layer, linux/mm_types.h).  The
 *     CPU address of a page is its address in the direct map; there is no
 *     highmem (PageHighMem is 0).
 *   - A temporary mapping with another cache attribute (kmap_local_page_prot,
 *     io_mapping_map_local_wc) is a kernel virtual address range entered with
 *     pmap_kenter_pa and the PMAP_* flags of the protection value (drm618_mm.h:
 *     pgprot_t holds VM_PROT_* and PMAP_* flags).  The direct map is not
 *     changed: a page that has a write-combined or uncached mapping is also
 *     visible cached through the direct map, as on the 5.6 core (the pages
 *     of TTM objects are mapped with the attribute at the place of use).
 *   - ioremap and friends map a physical range with pmap_kenter_pa; the
 *     ranges are recorded (drm618_io.c) because iounmap has no size.
 */

#ifndef _DRM618_PAGE_H_
#define _DRM618_PAGE_H_

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>

#include <uvm/uvm_extern.h>

#include <asm/page.h>
#include <linux/err.h>
#include <linux/gfp.h>
#include <linux/mm_types.h>

/* ---- the result of a fault -------------------------------------------- */

typedef unsigned int vm_fault_t;
#define	VM_FAULT_OOM		0x000001
#define	VM_FAULT_SIGBUS		0x000002
#define	VM_FAULT_NOPAGE		0x000100
#define	VM_FAULT_RETRY		0x000400

struct vm_area_struct;

struct vm_fault {
	struct vm_area_struct	*vma;
	unsigned long		address;
	unsigned long		pgoff;
	unsigned int		flags;
	struct page		*page;
};

/* ---- the pages ---------------------------------------------------------- */

/* The address of a page in the direct map (out of line: pmap.h is not read early enough here). */
void	*page_address(struct page *);

#define	PageHighMem(page)	((void)(page), false)

static inline void
clear_page(void *p)
{

	memset(p, 0, PAGE_SIZE);
}

void	clear_highpage(struct page *);
void	copy_highpage(struct page *, struct page *);

/* The page of a shmem file is wired while held: dropping the reference unwires it. */
void	put_page(struct page *);

static inline void
mark_page_accessed(struct page *page __unused)
{
}

/* Temporary CPU mapping of one page with the cache attribute of prot. */
void	*kmap_local_page_prot(struct page *, pgprot_t);
void	kunmap_local(const void *);

static inline void *
kmap_local_page(struct page *page)
{

	return page_address(page);
}

/* The page that shmem_read_folio_gfp returns, as a page. */
struct page *shmem_read_mapping_page_gfp(struct address_space *, unsigned long, gfp_t);

static inline pgprot_t
pgprot_noncached(pgprot_t prot)
{

	return prot | PMAP_NOCACHE;
}

/* ---- physical ranges ---------------------------------------------------- */

#define	MEMREMAP_WB	(1 << 0)
#define	MEMREMAP_WT	(1 << 1)
#define	MEMREMAP_WC	(1 << 2)
#define	MEMREMAP_ENC	(1 << 3)
#define	MEMREMAP_DEC	(1 << 4)

void	*drm618_ioremap(paddr_t, size_t, int /* PMAP_* cache flags */);
void	drm618_iounmap(void *);

/* Functions, not macros: ioremap, iounmap... are also member names (struct drm_gem_object_funcs.vunmap). */
static inline void *
ioremap(paddr_t pa, size_t size)
{

	return drm618_ioremap(pa, size, PMAP_NOCACHE);
}

static inline void *
ioremap_wc(paddr_t pa, size_t size)
{

	return drm618_ioremap(pa, size, PMAP_WRITE_COMBINE);
}

static inline void *
ioremap_cache(paddr_t pa, size_t size)
{

	return drm618_ioremap(pa, size, PMAP_WRITE_BACK);
}

static inline void
iounmap(void *va)
{

	drm618_iounmap(va);
}

static inline void *
memremap(paddr_t offset, size_t size, unsigned long flags)
{
	int cache = PMAP_WRITE_BACK;

	if (flags & MEMREMAP_WC)
		cache = PMAP_WRITE_COMBINE;
	else if ((flags & MEMREMAP_WB) == 0 && (flags & MEMREMAP_WT))
		cache = PMAP_NOCACHE;
	return drm618_ioremap(offset, size, cache);
}

static inline void
memunmap(void *va)
{

	drm618_iounmap(va);
}

/* vmap/vunmap of 6.18: unmap takes no size, the range is recorded (drm618_io.c style). */
void	*drm618_vmap(struct page **, unsigned int, int /* PMAP_* */, int /* VM_PROT_* */);

/* ---- single-segment scatter lists -------------------------------------- */

/*
 * A table of the layer (linux/scatterlist.h) is one entry that holds all
 * pages: sg_next of the one entry is the end of the list.  A walk over the
 * segments of a table that has several (ttm_kmap_iter_iomap_init) is not
 * supported.
 */
static inline struct scatterlist *
sg_next(struct scatterlist *sg __unused)
{

	return NULL;
}

/* ---- more of what TTM calls ------------------------------------------------- */

/* One page of kernel memory: alloc_page (GFP_DMA32 below 4 GiB, __GFP_ZERO zeroed) and __free_page. */
struct page *alloc_page(gfp_t);
void	__free_page(struct page *);

/* The page of a panic-time mapping is the plain direct map (no highmem). */
static inline void *
kmap_local_page_try_from_panic(struct page *page)
{

	return page_address(page);
}

/* init_on_free is not a feature here. */
static inline bool
want_init_on_free(void)
{

	return false;
}

/* No node of memory: the work runs on the queue. */
#define	queue_work_node(node, wq, work)	queue_work((wq), (work))

#define	dev_to_node(dev)	((void)(dev), NUMA_NO_NODE)

/* mappings of an io_mapping (linux/io-mapping.h) one page at a time; the bus address is the physical address (x86, ARM64 memory BARs) */
struct io_mapping;
void	*io_mapping_map_local_wc(struct io_mapping *, resource_size_t);
void	io_mapping_unmap_local(void *);

/* ---- the CPU model the 6.18 TTM reads ----------------------------------- */

/* ttm_prot_from_caching tests x86 family > 3 before it asks for uncached. */
struct drm618_cpuinfo {
	int	x86;
};
extern const struct drm618_cpuinfo drm618_boot_cpu_data;
#define	boot_cpu_data	drm618_boot_cpu_data

#define	NUMA_NO_NODE	(-1)

#endif	/* _DRM618_PAGE_H_ */
