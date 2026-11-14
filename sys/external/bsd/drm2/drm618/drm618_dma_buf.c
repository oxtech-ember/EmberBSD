/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD.  The dma-buf of the 6.18 core, written to the interface
 * include/linux/dma-buf.h of this directory and to the way drm_prime.c of
 * dist618 (MIT) uses it.  The reservation poll and kqueue behaviour follow
 * linux_dma_buf.c of the shared layer (Copyright (c) 2018 The NetBSD
 * Foundation, Inc., BSD-2-Clause), which keeps the 5.6 core's dma-buf as a
 * NetBSD file; here a dma-buf is a Linux file (drm618_file.h) and the file
 * operations of NetBSD call these.
 *
 * Not provided, and said so by the error: the ioctls of a dma-buf descriptor
 * (DMA_BUF_IOCTL_SYNC and the sync file import and export: ENOTTY), kqueue
 * (EINVAL, as dma_resv_kqfilter), the DMA mapping of a table through a struct
 * device (dma_map_sgtable: -EOPNOTSUPP).
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/atomic.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/poll.h>
#include <sys/systm.h>

#include <drm618_mm.h>
#include <linux/dma-buf.h>

static void
drm618_dma_buf_said(const char *what)
{
	static volatile unsigned said;

	if (atomic_swap_uint(&said, 1) == 0)
		printf("drm618: %s\n", what);
}

/* ---- the file of a dma-buf ------------------------------------------- */

static int
dma_buf_release_file(struct inode *inode __unused, struct file *file)
{
	struct dma_buf *dmabuf = file->private_data;

	dma_resv_poll_fini(&dmabuf->db_resv_poll);
	/* The exporter's object goes while the dma-buf is still valid. */
	dmabuf->ops->release(dmabuf);
	KASSERT(list_empty(&dmabuf->attachments));
	mutex_destroy(&dmabuf->lock);
	if (dmabuf->resv == &dmabuf->db_resv_int)
		dma_resv_fini(dmabuf->resv);
	kmem_free(dmabuf, sizeof(*dmabuf));
	return 0;
}

static int
dma_buf_mmap_file(struct file *file, struct vm_area_struct *vma)
{
	struct dma_buf *dmabuf = file->private_data;

	if (dmabuf->ops->mmap == NULL)
		return -EINVAL;
	if (vma->vm_pgoff + vma_pages(vma) > (dmabuf->size >> PAGE_SHIFT))
		return -EINVAL;
	return dmabuf->ops->mmap(dmabuf, vma);
}

/*
 * The events the caller asks for (the poll table of drm618_file.c carries
 * them): readable when no writer is pending, writable when nothing is.  The
 * waiting is the select queue of the reservation.
 */
static unsigned int
dma_buf_poll_file(struct file *file, struct poll_table_struct *pt)
{
	struct dma_buf *dmabuf = file->private_data;

	return dma_resv_do_poll(dmabuf->resv,
	    pt != NULL ? pt->pt_events : (POLLIN | POLLOUT | POLLRDNORM | POLLWRNORM),
	    &dmabuf->db_resv_poll);
}

static int
dma_buf_kqfilter_file(struct file *file, struct knote *kn)
{
	struct dma_buf *dmabuf = file->private_data;

	return dma_resv_kqfilter(dmabuf->resv, kn, &dmabuf->db_resv_poll);
}

/*
 * Only the size can be found out: seek to the end by offset 0 (and, as
 * Linux does, the beginning), which is what libraries use to learn it.
 */
static loff_t
dma_buf_llseek_file(struct file *file, loff_t offset, int whence)
{
	struct dma_buf *dmabuf = file->private_data;
	loff_t base;

	switch (whence) {
	case SEEK_END:
		base = (loff_t)dmabuf->size;
		break;
	case SEEK_SET:
		base = 0;
		break;
	default:
		return -EINVAL;
	}
	if (offset != 0)
		return -EINVAL;
	return base;
}

static const struct file_operations dma_buf_fops = {
	.release	= dma_buf_release_file,
	.mmap		= dma_buf_mmap_file,
	.poll		= dma_buf_poll_file,
	.llseek		= dma_buf_llseek_file,
	.bsd_kqfilter	= dma_buf_kqfilter_file,
};

/* ---- export, references, descriptor ---------------------------------- */

struct dma_buf *
dma_buf_export(const struct dma_buf_export_info *info)
{
	struct dma_buf *dmabuf;
	struct file *file;

	if (info->ops == NULL || info->ops->map_dma_buf == NULL ||
	    info->ops->unmap_dma_buf == NULL || info->ops->release == NULL)
		return ERR_PTR(-EINVAL);
	if (info->size == 0 || (info->size & (PAGE_SIZE - 1)) != 0)
		return ERR_PTR(-EINVAL);

	dmabuf = kmem_zalloc(sizeof(*dmabuf), KM_SLEEP);
	dmabuf->priv = info->priv;
	dmabuf->ops = info->ops;
	dmabuf->size = info->size;
	dmabuf->exp_name = info->exp_name;
	dmabuf->owner = info->owner;
	dmabuf->resv = info->resv;
	INIT_LIST_HEAD(&dmabuf->attachments);
	mutex_init(&dmabuf->lock);
	dma_resv_poll_init(&dmabuf->db_resv_poll);
	if (dmabuf->resv == NULL) {
		dmabuf->resv = &dmabuf->db_resv_int;
		dma_resv_init(dmabuf->resv);
	}

	file = anon_inode_getfile("dmabuf", &dma_buf_fops, dmabuf,
	    info->flags & (O_ACCMODE | O_NONBLOCK));
	if (IS_ERR(file)) {
		dma_resv_poll_fini(&dmabuf->db_resv_poll);
		if (dmabuf->resv == &dmabuf->db_resv_int)
			dma_resv_fini(dmabuf->resv);
		mutex_destroy(&dmabuf->lock);
		kmem_free(dmabuf, sizeof(*dmabuf));
		return ERR_CAST(file);
	}
	dmabuf->file = file;
	return dmabuf;
}

struct dma_buf *
dma_buf_get(int fd)
{
	struct file *file;
	int error;

	if (fd < 0)
		return ERR_PTR(-EBADF);
	file = drm618_fget((unsigned int)fd, &error);
	if (file == NULL)
		return ERR_PTR(-error);
	if (file->f_op != &dma_buf_fops) {
		fput(file);
		return ERR_PTR(-EINVAL);
	}
	return file->private_data;
}

void
get_dma_buf(struct dma_buf *dmabuf)
{

	get_file(dmabuf->file);
}

void
dma_buf_put(struct dma_buf *dmabuf)
{

	fput(dmabuf->file);
}

/* The descriptor owns the reference the caller held, as in Linux. */
int
dma_buf_fd(struct dma_buf *dmabuf, int flags)
{
	int fd;

	if (flags & ~O_CLOEXEC)
		return -EINVAL;
	fd = get_unused_fd_flags(flags);
	if (fd < 0)
		return fd;
	fd_install(fd, dmabuf->file);
	return fd;
}

/* ---- attachments ------------------------------------------------------ */

struct dma_buf_attachment *
dma_buf_attach(struct dma_buf *dmabuf, struct device *dev)
{
	struct dma_buf_attachment *attach;
	int ret = 0;

	attach = kmem_zalloc(sizeof(*attach), KM_SLEEP);
	attach->dmabuf = dmabuf;
	attach->dev = dev;
	mutex_lock(&dmabuf->lock);
	if (dmabuf->ops->attach)
		ret = dmabuf->ops->attach(dmabuf, attach);
	if (ret == 0)
		list_add(&attach->node, &dmabuf->attachments);
	mutex_unlock(&dmabuf->lock);
	if (ret) {
		kmem_free(attach, sizeof(*attach));
		return ERR_PTR(ret);
	}
	return attach;
}

void
dma_buf_detach(struct dma_buf *dmabuf, struct dma_buf_attachment *attach)
{

	mutex_lock(&dmabuf->lock);
	list_del(&attach->node);
	if (dmabuf->ops->detach)
		dmabuf->ops->detach(dmabuf, attach);
	mutex_unlock(&dmabuf->lock);
	kmem_free(attach, sizeof(*attach));
}

struct sg_table *
dma_buf_map_attachment(struct dma_buf_attachment *attach,
    enum dma_data_direction dir)
{

	dma_resv_assert_held(attach->dmabuf->resv);
	return attach->dmabuf->ops->map_dma_buf(attach, dir);
}

void
dma_buf_unmap_attachment(struct dma_buf_attachment *attach,
    struct sg_table *sgt, enum dma_data_direction dir)
{

	dma_resv_assert_held(attach->dmabuf->resv);
	attach->dmabuf->ops->unmap_dma_buf(attach, sgt, dir);
}

struct sg_table *
dma_buf_map_attachment_unlocked(struct dma_buf_attachment *attach,
    enum dma_data_direction dir)
{
	struct sg_table *sgt;
	int ret;

	ret = dma_resv_lock_interruptible(attach->dmabuf->resv, NULL);
	if (ret)
		return ERR_PTR(ret);
	sgt = dma_buf_map_attachment(attach, dir);
	dma_resv_unlock(attach->dmabuf->resv);
	return sgt;
}

void
dma_buf_unmap_attachment_unlocked(struct dma_buf_attachment *attach,
    struct sg_table *sgt, enum dma_data_direction dir)
{

	dma_resv_lock(attach->dmabuf->resv, NULL);
	dma_buf_unmap_attachment(attach, sgt, dir);
	dma_resv_unlock(attach->dmabuf->resv);
}

/* ---- tables through a device ------------------------------------------ */

int
dma_map_sgtable(struct device *dev __unused, struct sg_table *sgt __unused,
    enum dma_data_direction dir __unused, unsigned long attrs __unused)
{

	drm618_dma_buf_said("dma_map_sgtable: a struct device of this layer has"
	    " no DMA tag, tables are not mapped for devices (EOPNOTSUPP)");
	return -EOPNOTSUPP;
}

/* Only a table that dma_map_sgtable mapped can be unmapped, and none is. */
void
dma_unmap_sgtable(struct device *dev __unused, struct sg_table *sgt,
    enum dma_data_direction dir __unused, unsigned long attrs __unused)
{

	KASSERT(sgt == NULL || sgt->sgl->sg_dmamap == NULL);
}

/*
 * The bus address of page idx of the one entry of a table, from the segments
 * of its DMA map; 0 for a table that is not mapped.
 */
bus_addr_t
drm618_sg_dma_page_address(const struct scatterlist *sg, unsigned int idx)
{
	bus_size_t off = (bus_size_t)idx << PAGE_SHIFT;
	int i;

	if (sg->sg_dmamap == NULL)
		return 0;
	for (i = 0; i < sg->sg_dmamap->dm_nsegs; i++) {
		const bus_dma_segment_t *seg = &sg->sg_dmamap->dm_segs[i];

		if (off < seg->ds_len)
			return seg->ds_addr + off;
		off -= seg->ds_len;
	}
	return 0;
}
