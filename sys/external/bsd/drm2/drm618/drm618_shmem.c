/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD; written to the interface in
 * include/drm618_mm.h, no Linux code.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>

#include <drm618_mm.h>

static int
shmem_release(struct inode *inode, struct file *filp __unused)
{
	struct uvm_object *uobj = inode->i_data.a_uobj;

	if (uobj != NULL)
		uao_detach(uobj);
	return 0;
}

static const struct file_operations shmem_fops = {
	.release = shmem_release,
};

struct file *
shmem_file_setup(const char *name __unused, loff_t size,
    unsigned long flags __unused)
{
	struct file *filp;

	if (size < 0)
		return ERR_PTR(-EINVAL);
	filp = anon_inode_getfile("shmem", &shmem_fops, NULL, O_RDWR);
	filp->f_inode->i_size = size;
	filp->f_inode->i_data.a_uobj = uao_create(PAGE_ALIGN(size), 0);
	return filp;
}

struct file *
shmem_file_setup_with_mnt(struct vfsmount *mnt __unused, const char *name,
    loff_t size, unsigned long flags)
{

	return shmem_file_setup(name, size, flags);
}

struct folio *
shmem_read_folio_gfp(struct address_space *mapping, unsigned long index,
    gfp_t gfp __unused)
{
	struct uvm_object *uobj = mapping->a_uobj;
	struct vm_page *pg;
	int error;

	if (uobj == NULL)
		return ERR_PTR(-EINVAL);
	error = uvm_obj_wirepages(uobj, (off_t)index << PAGE_SHIFT,
	    (off_t)(index + 1) << PAGE_SHIFT, NULL);
	if (error)
		return ERR_PTR(-error);
	rw_enter(uobj->vmobjlock, RW_READER);
	pg = uvm_pagelookup(uobj, (off_t)index << PAGE_SHIFT);
	rw_exit(uobj->vmobjlock);
	KASSERT(pg != NULL);
	return page_folio(container_of(pg, struct page, p_vmp));
}

void
__folio_batch_release(struct folio_batch *fbatch)
{
	unsigned i;

	for (i = 0; i < fbatch->nr; i++) {
		struct vm_page *pg = &fbatch->folios[i]->page.p_vmp;
		struct uvm_object *uobj = pg->uobject;

		if (uobj != NULL)
			uvm_obj_unwirepages(uobj, pg->offset,
			    pg->offset + PAGE_SIZE);
	}
	fbatch->nr = 0;
}
