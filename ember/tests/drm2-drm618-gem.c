/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Kernel-side checks of the page cache and mapping model of the 6.18 GEM code
 * (drm618/include/drm618_mm.h, drm618_shmem.c).
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>

#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/pagevec.h>
#include <linux/shmem_fs.h>

static int t_run, t_fail;

#define	CHECK(cond)	do {						\
	t_run++;							\
	if (!(cond)) {							\
		t_fail++;						\
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);	\
	}								\
} while (0)

int	drm2_drm618_gem_run(void);

int
drm2_drm618_gem_run(void)
{
	struct file *filp;
	struct address_space *mapping;
	struct folio *folio, *folio2;
	struct folio_batch fbatch;
	struct vm_area_struct vma;
	unsigned wire;

	t_run = t_fail = 0;

	filp = shmem_file_setup("t", 4 * PAGE_SIZE, VM_NORESERVE);
	CHECK(!IS_ERR(filp));
	mapping = filp->f_mapping;
	CHECK(mapping != NULL && mapping == file_inode(filp)->i_mapping);
	CHECK(file_inode(filp)->i_size == 4 * PAGE_SIZE);
	CHECK(mapping->a_uobj != NULL);

	/* a page of the object: wired while held, the same page every time */
	folio = shmem_read_folio_gfp(mapping, 1, mapping_gfp_mask(mapping));
	CHECK(!IS_ERR(folio));
	wire = folio->page.p_vmp.wire_count;
	CHECK(wire >= 1);
	CHECK(folio_nr_pages(folio) == 1);
	CHECK(folio_file_page(folio, 1) == &folio->page);
	CHECK(page_folio(&folio->page) == folio);
	CHECK(folio_pfn(folio) == (VM_PAGE_TO_PHYS(&folio->page.p_vmp) >> PAGE_SHIFT));
	CHECK(folio->page.p_vmp.offset == PAGE_SIZE);
	folio2 = shmem_read_folio_gfp(mapping, 1, GFP_KERNEL);
	CHECK(folio2 == folio);
	CHECK(folio->page.p_vmp.wire_count == wire + 1);
	folio_mark_dirty(folio);
	folio_mark_accessed(folio);

	/* the batch gives the references back */
	folio_batch_init(&fbatch);
	CHECK(folio_batch_count(&fbatch) == 0);
	CHECK(folio_batch_add(&fbatch, folio) == FOLIO_BATCH_SIZE - 1);
	CHECK(folio_batch_add(&fbatch, folio2) == FOLIO_BATCH_SIZE - 2);
	CHECK(folio_batch_count(&fbatch) == 2);
	check_move_unevictable_folios(&fbatch);
	__folio_batch_release(&fbatch);
	CHECK(folio_batch_count(&fbatch) == 0);
	CHECK(folio->page.p_vmp.wire_count == wire - 1);

	/* out of range of the object is not a page of it */
	{
		struct file *nf = anon_inode_getfile("x", NULL, NULL, O_RDWR);

		CHECK(IS_ERR(shmem_read_folio_gfp(nf->f_mapping, 0, GFP_KERNEL)));
		fput(nf);
	}
	fput(filp);

	/* the mapping description */
	memset(&vma, 0, sizeof(vma));
	vma.vm_start = 0x10000;
	vma.vm_end = 0x10000 + 3 * PAGE_SIZE;
	CHECK(vma_pages(&vma) == 3);
	vm_flags_set(&vma, VM_IO | VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP);
	CHECK((vma.vm_flags & VM_IO) && (vma.vm_flags & VM_DONTEXPAND));
	vm_flags_clear(&vma, VM_IO);
	CHECK(!(vma.vm_flags & VM_IO) && (vma.vm_flags & VM_PFNMAP));
	CHECK(vm_get_page_prot(VM_READ | VM_WRITE) == (VM_PROT_READ | VM_PROT_WRITE));
	CHECK(vm_get_page_prot(VM_READ) == VM_PROT_READ);
	CHECK(pgprot_writecombine(vm_get_page_prot(VM_READ)) != vm_get_page_prot(VM_READ));
	CHECK(pgprot_decrypted(5) == 5);

	if (t_fail != 0)
		printf("drm2-drm618-gem: %d of %d checks FAILED\n", t_fail, t_run);
	else
		printf("drm2-drm618-gem: %d checks, 0 failed\n", t_run);
	return t_fail;
}
