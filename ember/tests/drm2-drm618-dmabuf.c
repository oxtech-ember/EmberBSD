/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Kernel-side checks of the dma-buf of the 6.18 core (drm618/drm618_dma_buf.c,
 * drm618/include/linux/dma-buf.h): export with its argument checks, the file
 * of a dma-buf and its references, descriptors, attach and detach, the
 * mapping hooks, mmap and llseek of the file, poll against the reservation,
 * and the scatter/gather table helpers drm_prime.c uses.  An exporter of the
 * test stands in for a driver.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/poll.h>
#include <sys/filedesc.h>

#include <linux/dma-buf.h>
#include <linux/dma-fence.h>
#include <linux/dma-resv.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/shmem_fs.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

static int t_run, t_fail;

#define	CHECK(cond)	do {						\
	t_run++;							\
	if (!(cond)) {							\
		t_fail++;						\
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);	\
	}								\
} while (0)

static int released, attached, detached, mapped, unmapped, mmapped, resv_locked_in_map;
static int attach_result;
static struct sg_table t_sgt;

static int
t_attach(struct dma_buf *buf __unused, struct dma_buf_attachment *attach __unused)
{

	attached++;
	return attach_result;
}

static void
t_detach(struct dma_buf *buf __unused, struct dma_buf_attachment *attach __unused)
{

	detached++;
}

static struct sg_table *
t_map(struct dma_buf_attachment *attach, enum dma_data_direction dir __unused)
{

	mapped++;
	resv_locked_in_map = dma_resv_is_locked(attach->dmabuf->resv);
	return &t_sgt;
}

static void
t_unmap(struct dma_buf_attachment *attach __unused, struct sg_table *sgt __unused,
    enum dma_data_direction dir __unused)
{

	unmapped++;
}

static void
t_release(struct dma_buf *buf)
{

	released++;
	KASSERT(buf->priv != NULL);
}

static int
t_mmap(struct dma_buf *buf __unused, struct vm_area_struct *vma)
{

	mmapped++;
	return vma->vm_flags & VM_EXEC ? -EACCES : 0;
}

static const struct dma_buf_ops t_ops = {
	.attach = t_attach,
	.detach = t_detach,
	.map_dma_buf = t_map,
	.unmap_dma_buf = t_unmap,
	.release = t_release,
	.mmap = t_mmap,
};

/* a fence that nobody waits on but the reservation */
static const char *
t_fence_name(struct dma_fence *f __unused)
{

	return "dmabuf-test";
}

static const struct dma_fence_ops t_fence_ops = {
	.get_driver_name = t_fence_name,
	.get_timeline_name = t_fence_name,
};

int	drm2_drm618_dmabuf_run(void);

int
drm2_drm618_dmabuf_run(void)
{
	static int token;
	struct dma_buf_export_info info;
	struct dma_buf *buf, *buf2;
	struct dma_buf_attachment *at;
	struct sg_table *sgt;
	struct vm_area_struct vma;
	struct poll_table_struct pt;
	struct dma_resv resv;
	struct dma_fence *fence;
	struct page *pages[3];
	struct folio_batch fbatch;
	struct file *shm;
	struct sg_table tbl;
	struct sg_page_iter piter;
	struct scatterlist *sg;
	spinlock_t lock;
	int fd, i, n;

	t_run = t_fail = 0;
	released = attached = detached = mapped = unmapped = mmapped = 0;
	attach_result = 0;

	/* export: the arguments */
	memset(&info, 0, sizeof(info));
	info.size = 4 * PAGE_SIZE;
	info.priv = &token;
	info.ops = NULL;
	CHECK(PTR_ERR(dma_buf_export(&info)) == -EINVAL);	/* no operations */
	info.ops = &t_ops;
	info.size = 0;
	CHECK(PTR_ERR(dma_buf_export(&info)) == -EINVAL);	/* no size */
	info.size = PAGE_SIZE + 1;
	CHECK(PTR_ERR(dma_buf_export(&info)) == -EINVAL);	/* not a whole page */
	{
		static const struct dma_buf_ops bad = { .release = t_release };

		info.size = PAGE_SIZE;
		info.ops = &bad;
		CHECK(PTR_ERR(dma_buf_export(&info)) == -EINVAL); /* no map hooks */
	}
	CHECK(released == 0);

	/* export: the object */
	CHECK(drm618_dma_buf_busy() == 0);
	info.ops = &t_ops;
	info.size = 4 * PAGE_SIZE;
	info.flags = O_RDWR | O_CLOEXEC;
	buf = dma_buf_export(&info);
	CHECK(!IS_ERR(buf));
	CHECK(drm618_dma_buf_busy() == EBUSY);	/* the module cannot go now */
	CHECK(buf->size == 4 * PAGE_SIZE);
	CHECK(buf->priv == &token);
	CHECK(buf->ops == &t_ops);
	CHECK(buf->resv != NULL);		/* its own reservation */
	CHECK(buf->file != NULL);
	CHECK(buf->file->private_data == buf);
	CHECK((buf->file->f_mode & (FMODE_READ | FMODE_WRITE)) == (FMODE_READ | FMODE_WRITE));
	info.flags = 0;
	buf2 = dma_buf_export(&info);
	CHECK(!IS_ERR(buf2));
	CHECK((buf2->file->f_mode & FMODE_READ) && !(buf2->file->f_mode & FMODE_WRITE));
	dma_buf_put(buf2);
	CHECK(released == 1);			/* the only reference: released at once */

	/* references: the exporter's release at the last drop only */
	get_dma_buf(buf);
	dma_buf_put(buf);
	CHECK(released == 1);

	/* descriptors */
	CHECK(PTR_ERR(dma_buf_get(-1)) == -EBADF);
	CHECK(PTR_ERR(dma_buf_get(5000)) == -EBADF);
	CHECK(dma_buf_fd(buf, O_RDWR) == -EINVAL);	/* the access mode is the file's */
	get_dma_buf(buf);			/* the reference the descriptor takes over */
	fd = dma_buf_fd(buf, O_CLOEXEC);
	CHECK(fd >= 0);
	buf2 = dma_buf_get(fd);
	CHECK(buf2 == buf);
	dma_buf_put(buf2);
	CHECK(fd_getfile(fd) != NULL);
	CHECK(fd_close(fd) == 0);
	CHECK(released == 1);			/* the exporter's own reference is left */
	CHECK(PTR_ERR(dma_buf_get(fd)) == -EBADF);
	{
		/* a descriptor of another kind of file */
		struct file *f = anon_inode_getfile("x", NULL, NULL, O_RDWR);
		int fd2 = get_unused_fd_flags(0);

		fd_install(fd2, f);
		CHECK(PTR_ERR(dma_buf_get(fd2)) == -EINVAL);
		fd_getfile(fd2);
		CHECK(fd_close(fd2) == 0);
	}

	/* attach and detach */
	at = dma_buf_attach(buf, NULL);
	CHECK(!IS_ERR(at) && attached == 1);
	CHECK(at->dmabuf == buf);
	dma_buf_detach(buf, at);
	CHECK(detached == 1);
	attach_result = -ENOSYS;
	at = dma_buf_attach(buf, NULL);
	CHECK(PTR_ERR(at) == -ENOSYS);
	CHECK(detached == 1);			/* a failed attach is not detached */
	attach_result = 0;

	/* mapping hooks run with the reservation held (the unlocked calls take it) */
	at = dma_buf_attach(buf, NULL);
	sgt = dma_buf_map_attachment_unlocked(at, DMA_BIDIRECTIONAL);
	CHECK(sgt == &t_sgt && mapped == 1 && resv_locked_in_map);
	dma_buf_unmap_attachment_unlocked(at, sgt, DMA_BIDIRECTIONAL);
	CHECK(unmapped == 1);
	CHECK(!dma_resv_is_locked(buf->resv));
	dma_buf_detach(buf, at);

	/* the file operations: mmap range and the hook, llseek */
	memset(&vma, 0, sizeof(vma));
	vma.vm_end = 4 * PAGE_SIZE;
	CHECK(buf->file->f_op->mmap(buf->file, &vma) == 0 && mmapped == 1);
	vma.vm_end = 5 * PAGE_SIZE;
	CHECK(buf->file->f_op->mmap(buf->file, &vma) == -EINVAL);
	vma.vm_end = 2 * PAGE_SIZE;
	vma.vm_pgoff = 3;
	CHECK(buf->file->f_op->mmap(buf->file, &vma) == -EINVAL);	/* runs past the end */
	vma.vm_pgoff = 2;
	CHECK(buf->file->f_op->mmap(buf->file, &vma) == 0);
	vma.vm_flags = VM_EXEC;
	vma.vm_pgoff = 0;
	CHECK(buf->file->f_op->mmap(buf->file, &vma) == -EACCES);	/* the hook's answer */
	CHECK(buf->file->f_op->llseek(buf->file, 0, SEEK_END) == 4 * PAGE_SIZE);
	CHECK(buf->file->f_op->llseek(buf->file, 0, SEEK_SET) == 0);
	CHECK(buf->file->f_op->llseek(buf->file, 0, SEEK_CUR) == -EINVAL);
	CHECK(buf->file->f_op->llseek(buf->file, 4096, SEEK_SET) == -EINVAL);
	CHECK(buf->file->f_op->bsd_kqfilter != NULL);
	CHECK(buf->file->f_op->unlocked_ioctl == NULL);		/* ioctls: ENOTTY from the file layer */

	/* poll: the reservation decides, the events asked for are returned */
	memset(&pt, 0, sizeof(pt));
	pt.pt_events = POLLIN | POLLOUT;
	CHECK(buf->file->f_op->poll(buf->file, &pt) == (POLLIN | POLLOUT));
	spin_lock_init(&lock);
	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	dma_fence_init(fence, &t_fence_ops, &lock, dma_fence_context_alloc(1), 1);
	dma_resv_lock(buf->resv, NULL);
	CHECK(dma_resv_reserve_fences(buf->resv, 1) == 0);
	dma_resv_add_fence(buf->resv, fence, DMA_RESV_USAGE_WRITE);
	dma_resv_unlock(buf->resv);
	pt.pt_events = POLLIN;
	CHECK(buf->file->f_op->poll(buf->file, &pt) == 0);	/* a writer is pending */
	pt.pt_events = POLLOUT;
	CHECK(buf->file->f_op->poll(buf->file, &pt) == 0);
	dma_fence_signal(fence);
	pt.pt_events = POLLIN | POLLOUT;
	CHECK(buf->file->f_op->poll(buf->file, &pt) == (POLLIN | POLLOUT));
	dma_fence_put(fence);

	/* the last reference: release runs once, with the attachments gone */
	dma_buf_put(buf);
	CHECK(released == 2);

	/* an exporter's own reservation is used and left alone */
	dma_resv_init(&resv);
	info.resv = &resv;
	info.flags = O_RDWR;
	buf = dma_buf_export(&info);
	CHECK(!IS_ERR(buf) && buf->resv == &resv);
	dma_buf_put(buf);
	CHECK(released == 3);
	CHECK(drm618_dma_buf_busy() == 0);	/* all released */
	dma_resv_fini(&resv);

	/* tables: pages, the iterators and the one segment */
	/* three pages of a shmem file (wired while held) */
	shm = shmem_file_setup("t", 3 * PAGE_SIZE, 0);
	folio_batch_init(&fbatch);
	for (i = 0; i < 3; i++) {
		struct folio *folio = shmem_read_folio_gfp(shm->f_mapping, i,
		    GFP_KERNEL);

		pages[i] = &folio->page;
		folio_batch_add(&fbatch, folio);
	}
	CHECK(sg_alloc_table_from_pages_segment(&tbl, pages, 3, 0, 3 * PAGE_SIZE,
	    UINT_MAX, GFP_KERNEL) == 0);
	n = 0;
	for_each_sgtable_page(&tbl, &piter, 0) {
		CHECK(sg_page_iter_page(&piter) == pages[n]);
		n++;
	}
	CHECK(n == 3);
	n = 0;
	for_each_sgtable_page(&tbl, &piter, 1)
		n++;
	CHECK(n == 2);				/* starting at the second page */
	n = 0;
	for_each_sgtable_dma_sg(&tbl, sg, i)
		n++;
	CHECK(n == 1);
	CHECK(drm618_sg_dma_page_address(tbl.sgl, 0) == 0);	/* not mapped */
	CHECK(dma_map_sgtable(NULL, &tbl, DMA_BIDIRECTIONAL, 0) == -EOPNOTSUPP);
	CHECK(drm618_sg_dma_page_address(tbl.sgl, 0) == 0);
	dma_unmap_sgtable(NULL, &tbl, DMA_BIDIRECTIONAL, 0);	/* nothing to undo */
	sg_free_table(&tbl);
	__folio_batch_release(&fbatch);
	fput(shm);

	if (t_fail != 0)
		printf("drm2-drm618-dmabuf: %d of %d checks FAILED\n", t_fail, t_run);
	else
		printf("drm2-drm618-dmabuf: %d checks, 0 failed\n", t_run);
	return t_fail;
}
