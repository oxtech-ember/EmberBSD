/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD.  Shadow of <linux/scatterlist.h> for the 6.18 core: the
 * table of the shared layer (one entry that holds all pages, drmkms_linux)
 * and the names drm_prime.c of dist618 (MIT) uses on it: the segment limit of
 * sg_alloc_table_from_pages_segment (a table here has one segment), and the
 * page iterators over that one entry.
 */

#ifndef _DRM618_LINUX_SCATTERLIST_H_
#define _DRM618_LINUX_SCATTERLIST_H_

#include_next <linux/scatterlist.h>

/* A table has one segment: the limit is not used. */
static inline int
sg_alloc_table_from_pages_segment(struct sg_table *sgt, struct page **pages,
    unsigned int npages, unsigned long offset, unsigned long size,
    unsigned long max_segment __unused, gfp_t gfp)
{

	return sg_alloc_table_from_pages(sgt, pages, npages, offset, size, gfp);
}

/*
 * Iterators: the page index in the entry.  start() is positioned before the
 * page of pgoffset, next() steps and says whether there is a page.
 */
struct sg_page_iter {
	struct scatterlist	*sg;
	unsigned int		sg_pgoffset;
	bool			started;
};

struct sg_dma_page_iter {
	struct sg_page_iter	base;
};

static inline void
__sg_page_iter_start(struct sg_page_iter *piter, struct scatterlist *sglist,
    unsigned int nents __unused, unsigned long pgoffset)
{

	piter->sg = sglist;
	piter->sg_pgoffset = pgoffset;
	piter->started = false;
}

static inline bool
__sg_page_iter_next(struct sg_page_iter *piter)
{

	if (piter->started)
		piter->sg_pgoffset++;
	piter->started = true;
	return piter->sg_pgoffset < piter->sg->sg_npgs;
}

static inline struct page *
sg_page_iter_page(struct sg_page_iter *piter)
{

	return piter->sg->sg_pgs[piter->sg_pgoffset];
}

#define	for_each_sg_page(sglist, piter, nents, pgoffset)		\
	for (__sg_page_iter_start((piter), (sglist), (nents), (pgoffset)); \
	    __sg_page_iter_next(piter);)
#define	for_each_sgtable_page(sgt, piter, pgoffset)			\
	for_each_sg_page((sgt)->sgl, piter, (sgt)->nents, pgoffset)

/* The table is one entry (sgl[0]): the loop runs once. */
#define	for_each_sgtable_dma_sg(sgt, sg, i)				\
	for ((i) = 0, (sg) = (sgt)->sgl; (i) < 1; (i)++, (sg) = NULL)

/* The bus address of a page of a table that dma_map_sgtable mapped; 0 if it is not mapped. */
bus_addr_t drm618_sg_dma_page_address(const struct scatterlist *, unsigned int);

static inline void
__sg_page_iter_dma_start(struct sg_dma_page_iter *dma_iter,
    struct scatterlist *sglist, unsigned int nents, unsigned long pgoffset)
{

	__sg_page_iter_start(&dma_iter->base, sglist, nents, pgoffset);
}

static inline bool
__sg_page_iter_dma_next(struct sg_dma_page_iter *dma_iter)
{

	return __sg_page_iter_next(&dma_iter->base);
}

static inline bus_addr_t
sg_page_iter_dma_address(struct sg_dma_page_iter *dma_iter)
{

	return drm618_sg_dma_page_address(dma_iter->base.sg,
	    dma_iter->base.sg_pgoffset);
}

#define	for_each_sgtable_dma_page(sgt, dma_iter, pgoffset)		\
	for (__sg_page_iter_dma_start((dma_iter), (sgt)->sgl,		\
	    (sgt)->nents, (pgoffset)); __sg_page_iter_dma_next(dma_iter);)

#endif	/* _DRM618_LINUX_SCATTERLIST_H_ */
