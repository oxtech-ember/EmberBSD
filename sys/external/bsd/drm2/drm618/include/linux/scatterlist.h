/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD; written to the interface the MIT files of the 6.18 core
 * and of its drivers use (drm_prime.c, drm_gem.c, virtgpu_*.c), no Linux code.
 * Shadow of <linux/scatterlist.h> for the 6.18 core.
 *
 * A table is what Linux says it is: an array of entries, each a run of bytes
 * of one page or of physically contiguous pages (page, offset, length), with
 * the bus address and length after a mapping, in one allocation (no chains).
 * The shared layer's table (one entry that holds the array of all pages, used
 * by the 5.6 core) is read under other names so that nothing of this core can
 * mix the two: struct netbsd_sg_table, struct netbsd_scatterlist, and the
 * functions of the shared layer are not reachable from the 6.18 sources (the
 * names below are implemented in drm618_scatterlist.c).  A table made here
 * must not go to a function of the shared layer.
 *
 * sg_init_one over a kernel address range records the address as well (va):
 * the range is not necessarily physically contiguous in this layer (the heap
 * is not), so the user of the entry walks it by page, see drm618_sg_walk().
 *
 * PAGE_MASK is not used here: it is the mask of the offset in NetBSD's headers
 * and the mask of the page in the layer's <linux/mm.h>, whichever was read.
 */

#ifndef _DRM618_LINUX_SCATTERLIST_H_
#define _DRM618_LINUX_SCATTERLIST_H_

/* The shared layer's table, under its own names, and its function names. */
#define	scatterlist	netbsd_scatterlist
#define	sg_table	netbsd_sg_table
#include_next <linux/scatterlist.h>
#undef	scatterlist
#undef	sg_table
#undef	sg_alloc_table
#undef	sg_alloc_table_from_pages
#undef	__sg_alloc_table_from_pages
#undef	sg_free_table
#undef	sg_dma_address
#undef	sg_dma_len
#undef	dma_map_sg
#undef	dma_map_sg_attrs
#undef	dma_unmap_sg
#undef	dma_unmap_sg_attrs

#include <sys/types.h>
#include <linux/types.h>
#include <linux/gfp.h>
#include <linux/mm_types.h>

struct device;

struct scatterlist {
	unsigned long	page_link;	/* struct page *, bit 1: last entry */
	unsigned int	offset;		/* of the first byte in the page */
	unsigned int	length;
	dma_addr_t	dma_address;
	unsigned int	dma_length;
	void		*va;		/* kernel address of the range, or NULL */
};

struct sg_table {
	struct scatterlist	*sgl;
	unsigned int		nents;		/* entries mapped for the device */
	unsigned int		orig_nents;	/* entries allocated */
};

#define	SG_END_BIT	0x02UL
#define	SG_CHAIN_BIT	0x01UL	/* the entry is a link to the next array, not data */

/* Out of line: they need the physical address of a page (uvm). */
int	sg_alloc_table(struct sg_table *, unsigned int, gfp_t);
int	sg_alloc_table_from_pages_segment(struct sg_table *, struct page **,
	    unsigned int, unsigned long, unsigned long, unsigned long, gfp_t);
void	sg_free_table(struct sg_table *);
struct page	*drm618_nth_page(struct page *, unsigned long);
paddr_t		drm618_sg_phys(const struct scatterlist *);
/* The physical address of the kernel address va (0 if it is not mapped). */
paddr_t		drm618_kva_to_pa(const void *);
/* The bus address of every entry is its physical address (no IOMMU), see the .c. */
void		drm618_sgt_dma_physical(struct sg_table *);

/*
 * Visit the physically contiguous pieces of the entry in order: fn gets the
 * physical address and the length of each piece (an entry of a kernel address
 * range is cut at the pages, an entry of a run of pages is one piece); the
 * walk stops at the first non-zero result of fn and returns it.
 */
int	drm618_sg_walk(const struct scatterlist *,
	    int (*)(void *, paddr_t, size_t), void *);

static inline int
sg_alloc_table_from_pages(struct sg_table *sgt, struct page **pages,
    unsigned int npages, unsigned long offset, unsigned long size, gfp_t gfp)
{

	return sg_alloc_table_from_pages_segment(sgt, pages, npages, offset,
	    size, (unsigned long)-1, gfp);
}

static inline void
sg_init_table(struct scatterlist *sgl, unsigned int nents)
{

	memset(sgl, 0, nents * sizeof(*sgl));
	sgl[nents - 1].page_link |= SG_END_BIT;
}

static inline void
sg_mark_end(struct scatterlist *sg)
{

	sg->page_link |= SG_END_BIT;
}

static inline void
sg_unmark_end(struct scatterlist *sg)
{

	sg->page_link &= ~SG_END_BIT;
}

static inline bool
sg_is_last(const struct scatterlist *sg)
{

	return (sg->page_link & SG_END_BIT) != 0;
}

/* No chains: the next entry is the next one of the array. */
static inline bool
sg_is_chain(const struct scatterlist *sg)
{

	return (sg->page_link & SG_CHAIN_BIT) != 0;
}

static inline struct scatterlist *
sg_chain_ptr(const struct scatterlist *sg)
{

	return (struct scatterlist *)(sg->page_link &
	    ~(unsigned long)(SG_END_BIT | SG_CHAIN_BIT));
}

/* the last entry of PRV (PRV_NENTS entries) becomes a link to the array SGL; it no longer holds data */
static inline void
sg_chain(struct scatterlist *prv, unsigned int prv_nents,
    struct scatterlist *sgl)
{
	struct scatterlist *link = &prv[prv_nents - 1];

	link->offset = 0;
	link->length = 0;
	link->va = NULL;
	link->page_link = ((unsigned long)sgl | SG_CHAIN_BIT) & ~SG_END_BIT;
}

static inline struct scatterlist *
sg_next(struct scatterlist *sg)
{

	if (sg_is_last(sg))
		return NULL;
	sg++;
	if (sg_is_chain(sg))
		sg = sg_chain_ptr(sg);
	return sg;
}

static inline struct page *
sg_page(const struct scatterlist *sg)
{

	return (struct page *)(sg->page_link &
	    ~(unsigned long)(SG_END_BIT | SG_CHAIN_BIT));
}

static inline void
sg_set_page(struct scatterlist *sg, struct page *page, unsigned int len,
    unsigned int offset)
{

	sg->page_link = (sg->page_link & SG_END_BIT) | (unsigned long)page;
	sg->offset = offset;
	sg->length = len;
	sg->va = NULL;
}

/* One range of kernel memory: the page of its first byte and the address. */
/* the page of an entry, its length and offset kept */
static inline void
sg_assign_page(struct scatterlist *sg, struct page *page)
{

	sg->page_link = (sg->page_link & SG_END_BIT) | (unsigned long)page;
	sg->va = NULL;
}

void	sg_set_buf(struct scatterlist *, const void *, unsigned int);

static inline void
sg_init_one(struct scatterlist *sg, const void *buf, unsigned int buflen)
{

	sg_init_table(sg, 1);
	sg_set_buf(sg, buf, buflen);
}

static inline paddr_t
sg_phys(const struct scatterlist *sg)
{

	return drm618_sg_phys(sg);
}

#define	sg_dma_address(sg)	((sg)->dma_address)
#define	sg_dma_len(sg)		((sg)->dma_length)

#define	for_each_sg(sglist, sg, nr, __i)				\
	for ((__i) = 0, (sg) = (sglist); (__i) < (nr); (__i)++, (sg) = sg_next(sg))

/* The entries of the table (orig_nents), and those the device sees (nents). */
#define	for_each_sgtable_sg(sgt, sg, i)					\
	for_each_sg((sgt)->sgl, sg, (sgt)->orig_nents, i)
#define	for_each_sgtable_dma_sg(sgt, sg, i)				\
	for_each_sg((sgt)->sgl, sg, (sgt)->nents, i)

/*
 * Iterators over the pages: the page of an entry follows from its first page
 * and the number of the page in the run.
 */
struct sg_page_iter {
	struct scatterlist	*sg;
	unsigned int		sg_pgoffset;	/* page in the entry */
	unsigned int		nents;		/* entries left including sg */
	int			__pg_advance;
};

struct sg_dma_page_iter {
	struct sg_page_iter	base;
};

static inline unsigned int
drm618_sg_npages(const struct scatterlist *sg, bool dma)
{
	unsigned long len = dma ? sg->dma_length : sg->length;
	unsigned long off = dma ? (unsigned long)(sg->dma_address & (PAGE_SIZE - 1)) :
	    sg->offset;

	return (len + off + PAGE_SIZE - 1) >> PAGE_SHIFT;
}

static inline bool
__sg_page_iter_next_common(struct sg_page_iter *piter, bool dma)
{

	if (piter->nents == 0)
		return false;
	if (piter->__pg_advance)
		piter->sg_pgoffset++;
	piter->__pg_advance = 1;
	while (piter->sg_pgoffset >= drm618_sg_npages(piter->sg, dma)) {
		piter->sg_pgoffset -= drm618_sg_npages(piter->sg, dma);
		piter->sg = sg_next(piter->sg);
		if (--piter->nents == 0 || piter->sg == NULL)
			return false;
	}
	return true;
}

static inline void
__sg_page_iter_start(struct sg_page_iter *piter, struct scatterlist *sglist,
    unsigned int nents, unsigned long pgoffset)
{

	piter->sg = sglist;
	piter->sg_pgoffset = pgoffset;
	piter->nents = nents;
	piter->__pg_advance = 0;
}

static inline bool
__sg_page_iter_next(struct sg_page_iter *piter)
{

	return __sg_page_iter_next_common(piter, false);
}

static inline struct page *
sg_page_iter_page(struct sg_page_iter *piter)
{

	return drm618_nth_page(sg_page(piter->sg), piter->sg_pgoffset);
}

#define	for_each_sg_page(sglist, piter, nents, pgoffset)		\
	for (__sg_page_iter_start((piter), (sglist), (nents), (pgoffset)); \
	    __sg_page_iter_next(piter);)
#define	for_each_sgtable_page(sgt, piter, pgoffset)			\
	for_each_sg_page((sgt)->sgl, piter, (sgt)->orig_nents, pgoffset)

static inline void
__sg_page_iter_dma_start(struct sg_dma_page_iter *dma_iter,
    struct scatterlist *sglist, unsigned int nents, unsigned long pgoffset)
{

	__sg_page_iter_start(&dma_iter->base, sglist, nents, pgoffset);
}

static inline bool
__sg_page_iter_dma_next(struct sg_dma_page_iter *dma_iter)
{

	return __sg_page_iter_next_common(&dma_iter->base, true);
}

/* The bus address of the page the iterator is at. */
static inline dma_addr_t
sg_page_iter_dma_address(struct sg_dma_page_iter *dma_iter)
{

	return (dma_iter->base.sg->dma_address & ~(dma_addr_t)(PAGE_SIZE - 1)) +
	    ((dma_addr_t)dma_iter->base.sg_pgoffset << PAGE_SHIFT);
}

#define	for_each_sgtable_dma_page(sgt, dma_iter, pgoffset)		\
	for (__sg_page_iter_dma_start((dma_iter), (sgt)->sgl,		\
	    (sgt)->nents, (pgoffset)); __sg_page_iter_dma_next(dma_iter);)

#endif	/* _DRM618_LINUX_SCATTERLIST_H_ */
