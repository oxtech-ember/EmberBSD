/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD; written to the interface the MIT files of the 6.18 core
 * and of its drivers use, see drm618/include/linux/scatterlist.h, no Linux
 * code.
 *
 * Tables and entries of the 6.18 core: allocation, the table of a page array
 * (runs of physically contiguous pages), the physical address of an entry,
 * the walk of an entry by physically contiguous pieces, and the bus address
 * of a table for a device that sees physical memory (no IOMMU).
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>

#include <uvm/uvm_extern.h>

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <asm/page.h>

#include <drm618_mm.h>

paddr_t
drm618_kva_to_pa(const void *va)
{
	paddr_t pa;

	if (!pmap_extract(pmap_kernel(), (vaddr_t)va, &pa))
		return 0;
	return pa;
}

struct page *
drm618_kva_to_page(const void *va)
{
	paddr_t pa = drm618_kva_to_pa(va);
	struct vm_page *pg;

	if (pa == 0)
		return NULL;
	pg = PHYS_TO_VM_PAGE(pa & ~(paddr_t)(PAGE_SIZE - 1));
	return pg == NULL ? NULL : container_of(pg, struct page, p_vmp);
}

struct page *
drm618_nth_page(struct page *page, unsigned long n)
{
	struct vm_page *pg;

	pg = PHYS_TO_VM_PAGE(page_to_phys(page) + (paddr_t)n * PAGE_SIZE);
	KASSERT(pg != NULL);
	return container_of(pg, struct page, p_vmp);
}

paddr_t
drm618_sg_phys(const struct scatterlist *sg)
{

	return page_to_phys(sg_page(sg)) + sg->offset;
}

void
sg_set_buf(struct scatterlist *sg, const void *buf, unsigned int buflen)
{
	paddr_t pa = drm618_kva_to_pa(buf);
	struct vm_page *pg = pa != 0 ? PHYS_TO_VM_PAGE(pa & ~(paddr_t)(PAGE_SIZE - 1)) : NULL;

	if (pg == NULL) {
		printf("sg_set_buf: %p is not memory of the kernel\n", buf);
		sg_set_page(sg, NULL, 0, 0);
		sg->va = NULL;
		return;
	}
	sg_set_page(sg, container_of(pg, struct page, p_vmp), buflen,
	    pa & (PAGE_SIZE - 1));
	sg->va = __UNCONST(buf);
}

int
drm618_sg_walk(const struct scatterlist *sg,
    int (*fn)(void *, paddr_t, size_t), void *arg)
{
	const char *va;
	size_t remain, chunk;
	paddr_t pa;
	int error;

	if (sg->va == NULL) {
		if (sg_page(sg) == NULL)
			return -EFAULT;
		return sg->length == 0 ? 0 :
		    fn(arg, drm618_sg_phys(sg), sg->length);
	}
	va = sg->va;
	for (remain = sg->length; remain > 0; remain -= chunk, va += chunk) {
		pa = drm618_kva_to_pa(va);
		if (pa == 0)
			return -EFAULT;
		chunk = MIN(remain, PAGE_SIZE - ((uintptr_t)va & (PAGE_SIZE - 1)));
		error = fn(arg, pa, chunk);
		if (error != 0)
			return error;
	}
	return 0;
}

int
sg_alloc_table(struct sg_table *sgt, unsigned int nents, gfp_t gfp)
{

	memset(sgt, 0, sizeof(*sgt));
	if (nents == 0)
		return -EINVAL;
	if (nents > UINT_MAX / sizeof(*sgt->sgl))
		return -ENOMEM;
	sgt->sgl = kzalloc(nents * sizeof(*sgt->sgl), gfp);
	if (sgt->sgl == NULL)
		return -ENOMEM;
	sg_init_table(sgt->sgl, nents);
	sgt->nents = sgt->orig_nents = nents;
	return 0;
}

void
sg_free_table(struct sg_table *sgt)
{

	kfree(sgt->sgl);
	sgt->sgl = NULL;
	sgt->nents = sgt->orig_nents = 0;
}

/*
 * One entry per run of pages that follow each other in physical memory, a
 * run at most max_segment bytes (rounded down to pages); the table covers
 * size bytes from offset in the first page.
 */
int
sg_alloc_table_from_pages_segment(struct sg_table *sgt, struct page **pages,
    unsigned int npages, unsigned long offset, unsigned long size,
    unsigned long max_segment, gfp_t gfp)
{
	unsigned long maxpg, runpg, left;
	unsigned int i, first, nruns, r;
	struct scatterlist *sg;
	int error;

	memset(sgt, 0, sizeof(*sgt));
	if (npages == 0 || offset >= PAGE_SIZE ||
	    size > (unsigned long)npages * PAGE_SIZE - offset ||
	    size == 0)
		return -EINVAL;
	maxpg = max_segment / PAGE_SIZE;
	if (maxpg == 0)
		maxpg = 1;
	/* Pages the size does not reach are not part of the table. */
	npages = (offset + size + PAGE_SIZE - 1) >> PAGE_SHIFT;

	nruns = 0;
	for (i = 0; i < npages; i += runpg) {
		runpg = 1;
		while (i + runpg < npages && runpg < maxpg &&
		    page_to_phys(pages[i + runpg]) ==
		    page_to_phys(pages[i + runpg - 1]) + PAGE_SIZE)
			runpg++;
		nruns++;
	}
	error = sg_alloc_table(sgt, nruns, gfp);
	if (error != 0)
		return error;
	left = size;
	sg = sgt->sgl;
	first = 1;
	for (i = 0, r = 0; r < nruns; r++, sg = sg_next(sg), i += runpg) {
		unsigned long off = first ? offset : 0, len;

		runpg = 1;
		while (i + runpg < npages && runpg < maxpg &&
		    page_to_phys(pages[i + runpg]) ==
		    page_to_phys(pages[i + runpg - 1]) + PAGE_SIZE)
			runpg++;
		len = MIN(left, runpg * PAGE_SIZE - off);
		sg_set_page(sg, pages[i], len, off);
		left -= len;
		first = 0;
	}
	KASSERT(left == 0);
	return 0;
}

/*
 * The bus address of a table for a device that sees physical memory (the
 * bus address of a page is its physical address: no IOMMU, no bounce):
 * the DMA side of every entry is its physical side.  Not for a device behind
 * an address translation.
 */
void
drm618_sgt_dma_physical(struct sg_table *sgt)
{
	struct scatterlist *sg;
	unsigned int i;

	for_each_sgtable_sg(sgt, sg, i) {
		sg->dma_address = drm618_sg_phys(sg);
		sg->dma_length = sg->length;
	}
	sgt->nents = sgt->orig_nents;
}
