/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD.  Written to the interface of
 * include/drm/ttm/ttm_backup.h (MIT file of dist618), no Linux code.
 *
 * The backup of a ttm_tt's pages (swap-out) on the shmem model of the 6.18
 * core (drm618_mm.h): a backup is a shmem file, that is a uvm anonymous
 * object, and the handle of a backed-up page is its page index plus one.  The
 * pages of the object are what the pageout daemon can send to swap; this
 * file does not start any I/O itself (the `writeback' hint is ignored).
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>

#include <uvm/uvm_extern.h>
#include <uvm/uvm_object.h>

#include <linux/err.h>
#include <linux/file.h>
#include <linux/mm.h>

#include <drm/ttm/ttm_backup.h>

static inline pgoff_t
handle_to_idx(pgoff_t handle)
{

	return handle - 1;
}

/* Free the page of the backup at handle: the contents are not needed any more. */
void
ttm_backup_drop(struct file *backup, pgoff_t handle)
{
	struct uvm_object *uobj = backup->f_mapping->a_uobj;
	off_t start = (off_t)handle_to_idx(handle) << PAGE_SHIFT;

	rw_enter(uobj->vmobjlock, RW_WRITER);
	(void)(*uobj->pgops->pgo_put)(uobj, start, start + PAGE_SIZE, PGO_FREE);
	/* pgo_put unlocks uobj->vmobjlock. */
}

int
ttm_backup_copy_page(struct file *backup, struct page *dst, pgoff_t handle,
    bool intr __unused)
{
	pgoff_t idx = handle_to_idx(handle);
	struct page *from;

	from = shmem_read_mapping_page_gfp(backup->f_mapping, idx, GFP_KERNEL);
	if (IS_ERR(from))
		return PTR_ERR(from);
	copy_highpage(dst, from);
	put_page(from);
	return 0;
}

s64
ttm_backup_backup_page(struct file *backup, struct page *page,
    bool writeback __unused, pgoff_t idx, gfp_t page_gfp __unused,
    gfp_t alloc_gfp)
{
	struct page *to;

	to = shmem_read_mapping_page_gfp(backup->f_mapping, idx, alloc_gfp);
	if (IS_ERR(to))
		return PTR_ERR(to);
	copy_highpage(to, page);
	{
		struct vm_page *pg = &to->p_vmp;

		rw_enter(pg->uobject->vmobjlock, RW_WRITER);
		uvm_pagemarkdirty(pg, UVM_PAGE_STATUS_DIRTY);
		rw_exit(pg->uobject->vmobjlock);
	}
	put_page(to);
	return (s64)idx + 1;
}

void
ttm_backup_fini(struct file *backup)
{

	fput(backup);
}

/* The space the backup can still use: the free swap. */
u64
ttm_backup_bytes_avail(void)
{

	return (u64)(uvmexp.swpages - uvmexp.swpginuse) << PAGE_SHIFT;
}

struct file *
ttm_backup_shmem_create(loff_t size)
{

	return shmem_file_setup("ttm shmem backup", size, 0);
}
