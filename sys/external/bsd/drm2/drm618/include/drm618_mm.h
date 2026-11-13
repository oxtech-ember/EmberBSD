/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD; written to the interface the
 * 6.18 DRM core uses (drm_gem.c, drm_prime.c), no Linux code.
 *
 * Page cache and mapping model of the 6.18 core:
 *
 *   - A shared memory file (shmem_file_setup) is a Linux file (drm618_file.h)
 *     whose address_space holds a uvm_object (an anonymous object, uao).
 *   - A folio is one page here: struct folio is a struct page, the folio of a
 *     page is the page itself.  shmem_read_folio_gfp wires the page of the
 *     object (it stays resident, as pages held by the GEM object must), and
 *     releasing the batch that holds it unwires it.
 *   - struct vm_area_struct carries what a mmap handler of the core reads and
 *     sets; the mmap of a Linux file by user space is not provided yet
 *     (drm618_file.c: fo_mmap says EOPNOTSUPP).
 */

#ifndef _DRM618_MM_H_
#define _DRM618_MM_H_

#include <sys/types.h>
#include <sys/param.h>

#include <uvm/uvm_extern.h>
#include <uvm/uvm_object.h>

#include <drm618_file.h>
#include <linux/err.h>
#include <linux/gfp.h>
#include <linux/mm_types.h>
#include <linux/highmem.h>

struct folio {
	struct page	page;
};

struct vm_fault;

/* vm_flags of a mapping */
typedef unsigned long vm_flags_t;
#define	VM_READ		0x00000001UL
#define	VM_WRITE	0x00000002UL
#define	VM_EXEC		0x00000004UL
#define	VM_SHARED	0x00000008UL
#define	VM_IO		0x00004000UL
#define	VM_DONTEXPAND	0x00040000UL
#define	VM_PFNMAP	0x00000400UL
#define	VM_MIXEDMAP	0x10000000UL
#define	VM_DONTDUMP	0x04000000UL
#define	VM_NORESERVE	0x00200000UL	/* shmem_file_setup: do not account the memory */

struct vm_operations_struct {
	void	(*open)(struct vm_area_struct *);
	void	(*close)(struct vm_area_struct *);
	int	(*fault)(struct vm_fault *);
};

struct vm_area_struct {
	unsigned long			vm_start;
	unsigned long			vm_end;
	unsigned long			vm_pgoff;
	vm_flags_t			vm_flags;
	pgprot_t			vm_page_prot;
	const struct vm_operations_struct *vm_ops;
	void				*vm_private_data;
	struct DRM618_FILE_TAG		*vm_file;
};

static inline void
vm_flags_set(struct vm_area_struct *vma, vm_flags_t flags)
{

	vma->vm_flags |= flags;
}

static inline void
vm_flags_clear(struct vm_area_struct *vma, vm_flags_t flags)
{

	vma->vm_flags &= ~flags;
}

static inline unsigned long
vma_pages(const struct vm_area_struct *vma)
{

	return (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;
}

/* pgprot_t holds PMAP_* flags here (layer: asm/pgtable.h) and the protection in VM_PROT_*. */
static inline pgprot_t
vm_get_page_prot(vm_flags_t flags)
{

	return ((flags & VM_READ) ? VM_PROT_READ : 0) |
	    ((flags & VM_WRITE) ? VM_PROT_WRITE : 0) |
	    ((flags & VM_EXEC) ? VM_PROT_EXECUTE : 0);
}

static inline pgprot_t
pgprot_writecombine(pgprot_t prot)
{

	return prot | PMAP_WRITE_COMBINE;
}

/* No memory encryption. */
static inline pgprot_t
pgprot_decrypted(pgprot_t prot)
{

	return prot;
}

/* ---- the page cache ---------------------------------------------------- */

#define	mapping_gfp_mask(mapping)	((void)(mapping), GFP_KERNEL)

static inline gfp_t
mapping_gfp_constraint(struct address_space *mapping __unused, gfp_t gfp)
{

	return gfp;
}

/* The pages of a shared memory file are wired while held: nothing to pin. */
static inline void
mapping_set_unevictable(struct address_space *mapping __unused)
{
}

static inline void
mapping_clear_unevictable(struct address_space *mapping __unused)
{
}

#define	FOLIO_BATCH_SIZE	31

struct folio_batch {
	unsigned char		nr;
	struct folio		*folios[FOLIO_BATCH_SIZE];
};

static inline struct folio *
page_folio(struct page *page)
{

	return (struct folio *)page;
}

static inline struct page *
folio_file_page(struct folio *folio, unsigned long index __unused)
{

	return &folio->page;
}

static inline unsigned long
folio_nr_pages(const struct folio *folio __unused)
{

	return 1;
}

static inline unsigned long
folio_pfn(const struct folio *folio)
{

	return VM_PAGE_TO_PHYS(&folio->page.p_vmp) >> PAGE_SHIFT;
}

static inline void
folio_mark_dirty(struct folio *folio)
{
	struct vm_page *pg = &folio->page.p_vmp;

	if (pg->uobject != NULL) {
		rw_enter(pg->uobject->vmobjlock, RW_WRITER);
		uvm_pagemarkdirty(pg, UVM_PAGE_STATUS_DIRTY);
		rw_exit(pg->uobject->vmobjlock);
	}
}

static inline void
folio_mark_accessed(struct folio *folio __unused)
{
}

static inline void
folio_batch_init(struct folio_batch *fbatch)
{

	fbatch->nr = 0;
}

static inline unsigned
folio_batch_count(const struct folio_batch *fbatch)
{

	return fbatch->nr;
}

/* Adds the folio and returns the room that is left. */
static inline unsigned
folio_batch_add(struct folio_batch *fbatch, struct folio *folio)
{

	KASSERT(fbatch->nr < FOLIO_BATCH_SIZE);
	fbatch->folios[fbatch->nr++] = folio;
	return FOLIO_BATCH_SIZE - fbatch->nr;
}

static inline void
check_move_unevictable_folios(struct folio_batch *fbatch __unused)
{
}

/* Drops the references of the batch: unwires its pages (the wire of shmem_read_folio_gfp). */
void	__folio_batch_release(struct folio_batch *);

/* The page of index of the file, wired; ERR_PTR(-errno) if there is none. */
struct folio *shmem_read_folio_gfp(struct address_space *, unsigned long, gfp_t);

struct DRM618_FILE_TAG *shmem_file_setup(const char *, loff_t, unsigned long);
struct DRM618_FILE_TAG *shmem_file_setup_with_mnt(struct vfsmount *, const char *,
    loff_t, unsigned long);

#endif	/* _DRM618_MM_H_ */
