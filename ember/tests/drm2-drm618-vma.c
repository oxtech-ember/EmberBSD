/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Kernel-side checks of the user mapping of Linux files (drm618/drm618_vma.c):
 * what mmap(2) on a file of the layer accepts and refuses, what the mmap
 * handler of the file sees, and the life of the uvm object that is the
 * mapping: the references run vm_ops open and close, the last one releases the
 * file.  The faults themselves need a user map and are checked by the vgem
 * client.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/mman.h>
#include <sys/systm.h>

#include <uvm/uvm_extern.h>
#include <uvm/uvm_object.h>

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/mm.h>

static int t_run, t_fail;

#define	CHECK(cond)	do {						\
	t_run++;							\
	if (!(cond)) {							\
		t_fail++;						\
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);	\
	}								\
} while (0)

static int mmap_calls, opened, closed, released, mmap_result;
static unsigned long seen_pgoff, seen_len, seen_flags;
static void *seen_file;
static bool set_remap;

static void
t_open(struct vm_area_struct *vma __unused)
{

	opened++;
}

static void
t_close(struct vm_area_struct *vma __unused)
{

	closed++;
}

static vm_fault_t
t_fault(struct vm_fault *vmf __unused)
{

	return VM_FAULT_SIGBUS;
}

static const struct vm_operations_struct t_vm_ops = {
	.open = t_open,
	.close = t_close,
	.fault = t_fault,
};

static int
t_mmap(struct file *filp, struct vm_area_struct *vma)
{

	mmap_calls++;
	seen_pgoff = vma->vm_pgoff;
	seen_len = vma->vm_end - vma->vm_start;
	seen_flags = vma->vm_flags;
	seen_file = vma->vm_file;
	if (mmap_result)
		return mmap_result;
	if (set_remap)
		return remap_pfn_range(vma, vma->vm_start, 0x1234,
		    vma->vm_end - vma->vm_start, vma->vm_page_prot);
	vma->vm_ops = &t_vm_ops;
	vma->vm_private_data = filp;
	return 0;
}

static int
t_release(struct inode *inode __unused, struct file *filp __unused)
{

	released++;
	return 0;
}

static const struct file_operations t_fops = {
	.mmap = t_mmap,
	.release = t_release,
};

static const struct file_operations t_nommap_fops = {
	.release = t_release,
};

int	drm2_drm618_vma_run(void);

int
drm2_drm618_vma_run(void)
{
	struct file *f;
	struct uvm_object *uobj;
	off_t off;
	int flags, advice, maxprot, error;

	t_run = t_fail = 0;
	mmap_calls = opened = closed = released = mmap_result = 0;
	set_remap = false;

	f = anon_inode_getfile("t", &t_fops, NULL, O_RDWR);
	CHECK(f != NULL);

	/* what is refused, before the handler runs */
	uobj = NULL; off = 0; flags = MAP_PRIVATE;
	error = drm618_vma_mmap(f, &off, 4 * PAGE_SIZE, PROT_READ, &flags, &advice, &uobj, &maxprot);
	CHECK(error == EINVAL && uobj == NULL && mmap_calls == 0);	/* no copy on write */
	flags = MAP_SHARED; off = 0;
	CHECK(drm618_vma_mmap(f, &off, 0, PROT_READ, &flags, &advice, &uobj, &maxprot) == EINVAL);
	CHECK(drm618_vma_mmap(f, &off, PAGE_SIZE + 1, PROT_READ, &flags, &advice, &uobj, &maxprot) == EINVAL);
	off = 17;
	CHECK(drm618_vma_mmap(f, &off, PAGE_SIZE, PROT_READ, &flags, &advice, &uobj, &maxprot) == EINVAL);
	off = -(off_t)PAGE_SIZE;
	CHECK(drm618_vma_mmap(f, &off, PAGE_SIZE, PROT_READ, &flags, &advice, &uobj, &maxprot) == EINVAL);
	off = 0;
	CHECK(mmap_calls == 0);
	CHECK(drm618_vma_mmap(f, &off, PAGE_SIZE, PROT_READ | PROT_EXEC, &flags, &advice, &uobj, &maxprot) == EACCES);
	CHECK(mmap_calls == 0);
	{
		struct file *g = anon_inode_getfile("t", &t_nommap_fops, NULL, O_RDWR);

		CHECK(drm618_vma_mmap(g, &off, PAGE_SIZE, PROT_READ, &flags, &advice, &uobj, &maxprot) == ENODEV);
		fput(g);
	}
	{
		struct file *g = anon_inode_getfile("t", &t_fops, NULL, O_RDONLY);

		/* write access to a file that was opened read only */
		CHECK(drm618_vma_mmap(g, &off, PAGE_SIZE, PROT_READ | PROT_WRITE, &flags, &advice, &uobj, &maxprot) == EACCES);
		CHECK(mmap_calls == 0);
		fput(g);
	}

	/* the handler's refusal is passed on, as a NetBSD errno */
	mmap_result = -EINVAL;
	CHECK(drm618_vma_mmap(f, &off, PAGE_SIZE, PROT_READ, &flags, &advice, &uobj, &maxprot) == EINVAL);
	mmap_result = -EACCES;
	CHECK(drm618_vma_mmap(f, &off, PAGE_SIZE, PROT_READ, &flags, &advice, &uobj, &maxprot) == EACCES);
	mmap_result = 0;
	CHECK(mmap_calls == 2 && closed == 0);	/* a refused mapping is not closed */

	/* a mapping: the handler sees the range [0, len), the offset in pages, the flags */
	mmap_calls = 0;
	released = 0;			/* the refused files above were released */
	off = 5 * PAGE_SIZE;
	uobj = NULL;
	CHECK(drm618_vma_mmap(f, &off, 3 * PAGE_SIZE, PROT_READ | PROT_WRITE, &flags, &advice, &uobj, &maxprot) == 0);
	CHECK(mmap_calls == 1);
	CHECK(seen_pgoff == 5 && seen_len == 3 * PAGE_SIZE);
	CHECK((seen_flags & VM_SHARED) && (seen_flags & VM_READ) && (seen_flags & VM_WRITE) && !(seen_flags & VM_EXEC));
	CHECK(seen_file == f);
	CHECK(uobj != NULL && off == 0);		/* the uvm object covers the mapping from 0 */
	CHECK(maxprot == (VM_PROT_READ | VM_PROT_WRITE));
	CHECK(uobj->uo_refs == 1);

	/* references are vmas: open per reference, close per detach, release at the last */
	CHECK(opened == 0);
	(*uobj->pgops->pgo_reference)(uobj);
	CHECK(opened == 1 && uobj->uo_refs == 2);
	(*uobj->pgops->pgo_detach)(uobj);
	CHECK(closed == 1 && uobj->uo_refs == 1);
	fput(f);				/* the descriptor's reference: the mapping holds the file */
	CHECK(released == 0);
	(*uobj->pgops->pgo_detach)(uobj);
	CHECK(closed == 2);
	CHECK(released == 1);			/* the last detach released the file */

	/* a range of remap_pfn_range: recorded, no vm_ops, closed without a hook */
	f = anon_inode_getfile("t", &t_fops, NULL, O_RDWR);
	set_remap = true;
	off = 0;
	CHECK(drm618_vma_mmap(f, &off, 2 * PAGE_SIZE, PROT_READ, &flags, &advice, &uobj, &maxprot) == 0);
	CHECK(uobj != NULL && uobj->pgops->pgo_fault != NULL);
	fput(f);
	released = 0;
	(*uobj->pgops->pgo_detach)(uobj);
	CHECK(released == 1);
	set_remap = false;

	/* the module may go when no mapping is left */
	CHECK(drm618_vma_busy() == 0);

	if (t_fail != 0)
		printf("drm2-drm618-vma: %d of %d checks FAILED\n", t_fail, t_run);
	else
		printf("drm2-drm618-vma: %d checks, 0 failed\n", t_run);
	return t_fail;
}
