/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Kernel-side checks of the scatterlist of the 6.18 core
 * (drm618/drm618_scatterlist.c, drm618/include/linux/scatterlist.h), run
 * inside drmkms618_test: Linux-style tables (an array of entries, one per run
 * of physically contiguous pages), the iterators over pages, the entries of a
 * kernel address range and their walk by physically contiguous pieces, the
 * bus address of a table for a device that sees physical memory.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/kmem.h>
#include <sys/systm.h>

#include <uvm/uvm_extern.h>

#include <linux/err.h>
#include <linux/scatterlist.h>
#include <linux/vmalloc.h>
#include <drm618_page.h>

static int t_run, t_fail;

#define	CHECK(cond)	do {						\
	t_run++;							\
	if (!(cond)) {							\
		t_fail++;						\
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);	\
	}								\
} while (0)

struct walk {
	unsigned int	n;
	size_t		total;
	bool		in_page;	/* no piece crosses a page */
	bool		match;		/* the piece is where the address says */
	const char	*next_va;
};

static int
t_piece(void *arg, paddr_t pa, size_t len)
{
	struct walk *w = arg;

	w->n++;
	if ((pa & (PAGE_SIZE - 1)) + len > PAGE_SIZE)
		w->in_page = false;
	if (drm618_kva_to_pa(w->next_va) != pa)
		w->match = false;
	w->next_va += len;
	w->total += len;
	return 0;
}

static int
t_stop(void *arg, paddr_t pa __unused, size_t len __unused)
{
	unsigned int *n = arg;

	return ++*n == 2 ? -ENOSPC : 0;
}

int	drm2_sg_run(void);

int
drm2_sg_run(void)
{
	struct sg_table sgt, tbl;
	struct scatterlist *sg, one;
	struct sg_page_iter piter;
	struct sg_dma_page_iter diter;
	struct page *pages[6];
	struct page *p;
	struct walk w;
	unsigned int i, n, runs, stop;
	char *buf;
	paddr_t pa;

	t_run = t_fail = 0;

	/* a table of entries */
	CHECK(sg_alloc_table(&sgt, 0, GFP_KERNEL) == -EINVAL);
	CHECK(sg_alloc_table(&sgt, 4, GFP_KERNEL) == 0);
	CHECK(sgt.nents == 4 && sgt.orig_nents == 4);
	n = 0;
	for_each_sgtable_sg(&sgt, sg, i)
		n++;
	CHECK(n == 4);
	CHECK(sg_is_last(&sgt.sgl[3]) && !sg_is_last(&sgt.sgl[2]));
	CHECK(sg_next(&sgt.sgl[1]) == &sgt.sgl[2] && sg_next(&sgt.sgl[3]) == NULL);
	CHECK(sg_dma_address(&sgt.sgl[0]) == 0 && sg_dma_len(&sgt.sgl[0]) == 0);
	sg_free_table(&sgt);
	CHECK(sgt.sgl == NULL && sgt.nents == 0 && sgt.orig_nents == 0);

	/* four pages one after the other, then two single pages */
	for (i = 0; i < 6; i++) {
		pages[i] = alloc_page(GFP_KERNEL | __GFP_ZERO);
		CHECK(pages[i] != NULL);
	}
	if (t_fail != 0)
		goto out;
	/* the runs the table must have: those of the physical addresses */
	runs = 1;
	for (i = 1; i < 6; i++)
		if (page_to_phys(pages[i]) != page_to_phys(pages[i - 1]) + PAGE_SIZE)
			runs++;
	CHECK(sg_alloc_table_from_pages(&tbl, pages, 6, 0, 6 * PAGE_SIZE,
	    GFP_KERNEL) == 0);
	CHECK(tbl.orig_nents == runs && tbl.nents == runs);
	n = 0;
	for_each_sgtable_page(&tbl, &piter, 0) {
		CHECK(n < 6 && sg_page_iter_page(&piter) == pages[n]);
		n++;
	}
	CHECK(n == 6);
	n = 0;
	for_each_sgtable_page(&tbl, &piter, 4)
		n++;
	CHECK(n == 2);				/* from the fifth page */
	n = 0;
	for_each_sgtable_sg(&tbl, sg, i) {
		CHECK(sg_phys(sg) == page_to_phys(sg_page(sg)));
		n += sg->length;
	}
	CHECK(n == 6 * PAGE_SIZE);
	sg_free_table(&tbl);

	/* a smaller size and an offset: the table covers 3 pages from byte 100 */
	CHECK(sg_alloc_table_from_pages(&tbl, pages, 6, 100,
	    2 * PAGE_SIZE + 50, GFP_KERNEL) == 0);
	n = 0;
	for_each_sgtable_sg(&tbl, sg, i) {
		if (i == 0) {
			CHECK(sg->offset == 100);
			CHECK(sg_phys(sg) == page_to_phys(pages[0]) + 100);
		}
		n += sg->length;
	}
	CHECK(n == 2 * PAGE_SIZE + 50);
	n = 0;
	for_each_sgtable_page(&tbl, &piter, 0)
		n++;
	CHECK(n == 3);
	sg_free_table(&tbl);
	/* sizes that do not fit are refused, not trusted */
	CHECK(sg_alloc_table_from_pages(&tbl, pages, 2, 0, 3 * PAGE_SIZE,
	    GFP_KERNEL) == -EINVAL);
	CHECK(sg_alloc_table_from_pages(&tbl, pages, 2, PAGE_SIZE, PAGE_SIZE,
	    GFP_KERNEL) == -EINVAL);
	CHECK(sg_alloc_table_from_pages(&tbl, pages, 0, 0, PAGE_SIZE,
	    GFP_KERNEL) == -EINVAL);
	CHECK(sg_alloc_table_from_pages(&tbl, pages, 2, 0, 0,
	    GFP_KERNEL) == -EINVAL);

	/* the segment limit: no entry longer than 2 pages */
	CHECK(sg_alloc_table_from_pages_segment(&tbl, pages, 6, 0,
	    6 * PAGE_SIZE, 2 * PAGE_SIZE, GFP_KERNEL) == 0);
	n = 0;
	for_each_sgtable_sg(&tbl, sg, i) {
		CHECK(sg->length <= 2 * PAGE_SIZE);
		n += sg->length;
	}
	CHECK(n == 6 * PAGE_SIZE);
	CHECK(tbl.orig_nents >= 3);		/* 6 pages, 2 at most per entry */

	/* the bus address of a device that sees physical memory */
	CHECK(sg_dma_len(&tbl.sgl[0]) == 0);
	drm618_sgt_dma_physical(&tbl);
	n = 0;
	for_each_sgtable_dma_sg(&tbl, sg, i) {
		CHECK(sg_dma_address(sg) == sg_phys(sg));
		CHECK(sg_dma_len(sg) == sg->length);
		n++;
	}
	CHECK(n == tbl.nents);
	n = 0;
	for_each_sgtable_dma_page(&tbl, &diter, 0) {
		if (n >= 8)			/* never loop on a broken iterator */
			break;
		CHECK(n < 6 && sg_page_iter_dma_address(&diter) ==
		    page_to_phys(pages[n]));
		n++;
	}
	CHECK(n == 6);
	sg_free_table(&tbl);

	/* nth page */
	p = drm618_nth_page(pages[0], 0);
	CHECK(p == pages[0]);

	/* a range of kernel memory over three pages and a half, walked */
	buf = kmem_alloc(4 * PAGE_SIZE, KM_SLEEP);
	/* the heap is not vmalloc memory: the 6.18 sources would take it for page aligned */
	CHECK(!is_vmalloc_addr(buf + 20));
	sg_init_one(&one, buf + 20, 3 * PAGE_SIZE + 100);
	CHECK(one.length == 3 * PAGE_SIZE + 100 && one.va == buf + 20);
	CHECK(sg_is_last(&one) && sg_next(&one) == NULL);
	CHECK(sg_page(&one) != NULL);
	pa = drm618_kva_to_pa(buf + 20);
	CHECK(pa != 0);
	if (sg_page(&one) != NULL)
		CHECK(sg_phys(&one) == pa);
	CHECK(one.offset == (pa & (PAGE_SIZE - 1)));
	memset(&w, 0, sizeof(w));
	w.in_page = w.match = true;
	w.next_va = buf + 20;
	CHECK(drm618_sg_walk(&one, t_piece, &w) == 0);
	CHECK(w.total == 3 * PAGE_SIZE + 100 && w.in_page && w.match);
	CHECK(w.n >= 4 && w.n <= 5);	/* cut at the pages */
	stop = 0;
	CHECK(drm618_sg_walk(&one, t_stop, &stop) == -ENOSPC && stop == 2);
	kmem_free(buf, 4 * PAGE_SIZE);

	/* an entry of a page: one piece, where the page is */
	sg_init_table(&one, 1);
	sg_set_page(&one, pages[0], 1000, 24);
	CHECK(one.va == NULL && sg_phys(&one) == page_to_phys(pages[0]) + 24);
	memset(&w, 0, sizeof(w));
	w.in_page = w.match = true;
	w.next_va = (char *)page_address(pages[0]) + 24;
	CHECK(drm618_sg_walk(&one, t_piece, &w) == 0);
	CHECK(w.n == 1 && w.total == 1000 && w.match);

	/* an address that is no kernel memory: the entry has no page, the walk says so */
	sg_init_table(&one, 1);
	sg_set_buf(&one, (void *)(uintptr_t)PAGE_SIZE, 10);
	CHECK(sg_page(&one) == NULL && one.length == 0);
	CHECK(drm618_sg_walk(&one, t_piece, &w) == -EFAULT);
out:
	for (i = 0; i < 6; i++)
		if (pages[i] != NULL)
			__free_page(pages[i]);
	if (t_fail != 0)
		printf("drm2-sg: %d of %d checks FAILED\n", t_fail, t_run);
	else
		printf("drm2-sg: %d checks, 0 failed\n", t_run);
	return t_fail;
}
