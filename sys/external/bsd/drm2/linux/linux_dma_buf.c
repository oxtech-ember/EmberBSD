/*	$NetBSD: linux_dma_buf.c,v 1.17 2024/05/20 11:35:10 riastradh Exp $	*/

/*-
 * Copyright (c) 2018 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Taylor R. Campbell.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: linux_dma_buf.c,v 1.17 2024/05/20 11:35:10 riastradh Exp $");

#include <sys/types.h>
#include <sys/atomic.h>
#include <sys/file.h>
#include <sys/dmabuf.h>
#include <sys/filedesc.h>
#include <sys/kmem.h>
#include <sys/mutex.h>

#include <uvm/uvm_prot.h>

#include <linux/dma-buf.h>
#include <linux/dma-fence-array.h>
#include <linux/sync_file.h>
#include <linux/err.h>
#include <linux/dma-resv.h>

static int	dmabuf_fop_ioctl(struct file *, u_long, void *);
static int	dmabuf_fop_poll(struct file *, int);
static int	dmabuf_fop_close(struct file *);
static int	dmabuf_fop_kqfilter(struct file *, struct knote *);
static int	dmabuf_fop_mmap(struct file *, off_t *, size_t, int, int *,
		    int *, struct uvm_object **, int *);
static int	dmabuf_fop_seek(struct file *fp, off_t delta, int whence,
		    off_t *newoffp, int flags);

static const struct fileops dmabuf_fileops = {
	.fo_name = "dmabuf",
	.fo_read = fbadop_read,
	.fo_write = fbadop_write,
	.fo_ioctl = dmabuf_fop_ioctl,
	.fo_fcntl = fnullop_fcntl,
	.fo_poll = dmabuf_fop_poll,
	.fo_stat = fbadop_stat,
	.fo_close = dmabuf_fop_close,
	.fo_kqfilter = dmabuf_fop_kqfilter,
	.fo_restart = fnullop_restart,
	.fo_mmap = dmabuf_fop_mmap,
	.fo_seek = dmabuf_fop_seek,
};

/* A vector holds borrowed references until the snapshot owns its leaves. */
struct dmabuf_fence_vector {
	struct dma_fence **items;
	unsigned count;
	unsigned capacity;
};

static int
dmabuf_fence_append(struct dmabuf_fence_vector *v, struct dma_fence *fence)
{
	unsigned i;

	if (fence == NULL)
		return 0;
	for (i = 0; i < v->count; i++) {
		if (v->items[i] == fence)
			return 0;
	}
	if (v->count == v->capacity) {
		struct dma_fence **items;
		unsigned capacity;

		if (v->capacity >= INT_MAX / 2)
			return ENOMEM;
		capacity = v->capacity ? 2 * v->capacity : 8;
		items = kmalloc_array(capacity, sizeof(*items), GFP_KERNEL);
		if (items == NULL)
			return ENOMEM;
		if (v->count)
			memcpy(items, v->items, v->count * sizeof(*items));
		kfree(v->items);
		v->items = items;
		v->capacity = capacity;
	}
	v->items[v->count++] = fence;
	return 0;
}

/*
 * Take a real snapshot while the reservation lock excludes device submission.
 * The reservation and extra fence own all traversed references. Keep OR arrays
 * opaque. Visit each AND node once, so export/import loops do not grow trees.
 */
static int
dmabuf_fence_snapshot(struct dma_resv *resv, bool write,
    struct dma_fence *extra, struct dma_fence **result)
{
	struct dmabuf_fence_vector walk = {0}, leaves = {0};
	struct dma_resv_list *list;
	struct dma_fence_array *array;
	struct dma_fence *fence;
	unsigned i, j;
	int error, status;

	dma_resv_assert_held(resv);
	error = dmabuf_fence_append(&walk, dma_resv_get_excl(resv));
	if (error)
		goto out;
	if (write && (list = dma_resv_get_list(resv)) != NULL) {
		for (i = 0; i < list->shared_count; i++) {
			error = dmabuf_fence_append(&walk, list->shared[i]);
			if (error)
				goto out;
		}
	}
	error = dmabuf_fence_append(&walk, extra);
	if (error)
		goto out;
	for (i = 0; i < walk.count; i++) {
		fence = walk.items[i];
		status = dma_fence_get_status(fence);
		if (status > 0)
			continue;
		if (status == 0 && dma_fence_is_array(fence) &&
		    !(array = to_dma_fence_array(fence))->dfa_signal_on_any) {
			for (j = 0; j < array->num_fences; j++) {
				error = dmabuf_fence_append(&walk, array->fences[j]);
				if (error)
					goto out;
			}
		} else {
			error = dmabuf_fence_append(&leaves, fence);
			if (error)
				goto out;
		}
	}
	if (leaves.count == 0) {
		fence = dma_fence_get_stub();
	} else if (leaves.count == 1) {
		fence = dma_fence_get(leaves.items[0]);
	} else {
		for (i = 0; i < leaves.count; i++)
			dma_fence_get(leaves.items[i]);
		array = dma_fence_array_create((int)leaves.count, leaves.items,
		    dma_fence_context_alloc(1), 1, false);
		if (array == NULL) {
			for (i = 0; i < leaves.count; i++)
				dma_fence_put(leaves.items[i]);
			error = ENOMEM;
			goto out;
		}
		leaves.items = NULL; /* The array owns the vector and references. */
		fence = &array->base;
	}
	*result = fence;
	error = 0;
out:
	kfree(leaves.items);
	kfree(walk.items);
	return error;
}

static int
dmabuf_sync_flags(const struct file *fp, uint32_t flags)
{

	if (flags == 0 || (flags & ~DMA_BUF_SYNC_RW) != 0)
		return EINVAL;
	if (((flags & DMA_BUF_SYNC_READ) && !(fp->f_flag & FREAD)) ||
	    ((flags & DMA_BUF_SYNC_WRITE) && !(fp->f_flag & FWRITE)))
		return EACCES;
	return 0;
}

static int
dmabuf_export_sync_file(struct file *fp,
    struct dma_buf_export_sync_file *args)
{
	struct dma_buf *dmabuf = fp->f_data;
	struct dma_fence *fence;
	struct file *syncfp;
	int fd, error;

	error = dmabuf_sync_flags(fp, args->flags);
	if (error)
		return error;
	error = fd_allocfile(&syncfp, &fd);
	if (error)
		return error;
	error = -dma_resv_lock_interruptible(dmabuf->resv, NULL);
	if (error)
		goto abort;
	error = dmabuf_fence_snapshot(dmabuf->resv,
	    (args->flags & DMA_BUF_SYNC_WRITE) != 0, NULL, &fence);
	dma_resv_unlock(dmabuf->resv);
	if (error)
		goto abort;
	if (sync_file_create(fence, syncfp) == NULL) {
		dma_fence_put(fence);
		error = ENOMEM;
		goto abort;
	}
	dma_fence_put(fence);
	fd_set_exclose(curlwp, fd, true);
	fd_affix(curproc, syncfp, fd);
	args->fd = fd;
	return 0;
abort:
	fd_abort(curproc, syncfp, fd);
	return error;
}

static int
dmabuf_import_sync_file(struct file *fp,
    const struct dma_buf_import_sync_file *args)
{
	struct dma_buf *dmabuf = fp->f_data;
	struct dma_fence *fence, *merged;
	struct dma_resv_list *list;
	unsigned i;
	int error;

	error = dmabuf_sync_flags(fp, args->flags);
	if (error)
		return error;
	fence = sync_file_get_fence(args->fd);
	if (fence == NULL)
		return EINVAL;
	error = -dma_resv_lock_interruptible(dmabuf->resv, NULL);
	if (error)
		goto out;
	if (args->flags & DMA_BUF_SYNC_WRITE) {
		error = dmabuf_fence_snapshot(dmabuf->resv, false, fence, &merged);
		if (error)
			goto unlock;
		dma_resv_replace_excl_fence(dmabuf->resv, merged);
		dma_fence_put(merged);
	} else {
		/* Do not replace a later fence on the same ordered timeline. */
		list = dma_resv_get_list(dmabuf->resv);
		if (list != NULL) {
			for (i = 0; i < list->shared_count; i++) {
				if (list->shared[i]->context == fence->context &&
				    !dma_fence_is_later(fence, list->shared[i]))
					goto unlock;
			}
		}
		error = -dma_resv_reserve_shared(dmabuf->resv, 1);
		if (error)
			goto unlock;
		dma_resv_add_shared_fence(dmabuf->resv, fence);
	}
unlock:
	dma_resv_unlock(dmabuf->resv);
out:
	dma_fence_put(fence);
	return error;
}

static int
dmabuf_fop_ioctl(struct file *fp, u_long command, void *data)
{

	switch (command) {
	case DMA_BUF_IOCTL_EXPORT_SYNC_FILE:
		return dmabuf_export_sync_file(fp, data);
	case DMA_BUF_IOCTL_IMPORT_SYNC_FILE:
		return dmabuf_import_sync_file(fp, data);
	default:
		return ENOTTY;
	}
}

struct dma_buf *
dma_buf_export(struct dma_buf_export_info *info)
{
	struct dma_buf *dmabuf;

	if (info->ops == NULL || info->ops->release == NULL)
		return ERR_PTR(-EINVAL);

	if (info->resv == NULL) {
		dmabuf = kmem_zalloc(offsetof(struct dma_buf, db_resv_int[1]),
		    KM_SLEEP);
	} else {
		dmabuf = kmem_zalloc(sizeof(*dmabuf), KM_SLEEP);
	}

	dmabuf->priv = info->priv;
	dmabuf->ops = info->ops;
	dmabuf->size = info->size;
	dmabuf->resv = info->resv;

	mutex_init(&dmabuf->db_lock, MUTEX_DEFAULT, IPL_NONE);
	dmabuf->db_refcnt = 1;
	dma_resv_poll_init(&dmabuf->db_resv_poll);

	if (dmabuf->resv == NULL) {
		dmabuf->resv = &dmabuf->db_resv_int[0];
		dma_resv_init(dmabuf->resv);
	}

	return dmabuf;
}

int
dma_buf_fd(struct dma_buf *dmabuf, int flags)
{
	struct file *file;
	int fd, fflags;
	int ret;

	/* EmberBSD, AI-assisted: validate before transferring a reference. */
	if (flags & ~(O_ACCMODE | O_CLOEXEC))
		return -EINVAL;
	switch (flags & O_ACCMODE) {
	case O_RDONLY:
		fflags = FREAD;
		break;
	case O_WRONLY:
		fflags = FWRITE;
		break;
	case O_RDWR:
		fflags = FREAD | FWRITE;
		break;
	default:
		return -EINVAL;
	}

	/* XXX errno NetBSD->Linux */
	ret = -fd_allocfile(&file, &fd);
	if (ret)
		goto out0;

	/* On success the file owns the caller's reference, as on Linux. */

	file->f_type = DTYPE_MISC;
	file->f_flag = fflags;
	file->f_ops = &dmabuf_fileops;
	file->f_data = dmabuf;
	fd_set_exclose(curlwp, fd, (flags & O_CLOEXEC) != 0);
	fd_affix(curproc, file, fd);

	ret = fd;
out0:	return ret;
}

struct dma_buf *
dma_buf_get(int fd)
{
	struct file *file;
	struct dma_buf *dmabuf;
	unsigned refcnt __diagused;
	int error;

	if ((file = fd_getfile(fd)) == NULL) {
		error = EBADF;
		goto fail0;
	}
	if (file->f_type != DTYPE_MISC || file->f_ops != &dmabuf_fileops) {
		error = EINVAL;
		goto fail1;
	}

	dmabuf = file->f_data;
	refcnt = atomic_inc_uint_nv(&dmabuf->db_refcnt);
	KASSERT(refcnt > 1);
	fd_putfile(fd);
	return dmabuf;

fail1:	fd_putfile(fd);
fail0:	KASSERT(error);
	return ERR_PTR(-error);
}

void
get_dma_buf(struct dma_buf *dmabuf)
{
	unsigned refcnt __diagused;

	refcnt = atomic_inc_uint_nv(&dmabuf->db_refcnt);
	KASSERT(refcnt > 1);
}

void
dma_buf_put(struct dma_buf *dmabuf)
{

	membar_release();
	if (atomic_dec_uint_nv(&dmabuf->db_refcnt) != 0)
		return;
	membar_acquire();

	dma_resv_poll_fini(&dmabuf->db_resv_poll);
	/* Release the exporter's object while the dma-buf is still valid. */
	dmabuf->ops->release(dmabuf);
	mutex_destroy(&dmabuf->db_lock);
	if (dmabuf->resv == &dmabuf->db_resv_int[0]) {
		dma_resv_fini(dmabuf->resv);
		kmem_free(dmabuf, offsetof(struct dma_buf, db_resv_int[1]));
	} else {
		kmem_free(dmabuf, sizeof(*dmabuf));
	}
}

struct dma_buf_attachment *
dma_buf_dynamic_attach(struct dma_buf *dmabuf, bus_dma_tag_t dmat,
    bool dynamic_mapping)
{
	struct dma_buf_attachment *attach;
	int ret = 0;

	attach = kmem_zalloc(sizeof(*attach), KM_SLEEP);
	attach->dmabuf = dmabuf;
	attach->dev = dmat;
	attach->dynamic_mapping = dynamic_mapping;

	mutex_enter(&dmabuf->db_lock);
	if (dmabuf->ops->attach)
		ret = dmabuf->ops->attach(dmabuf, attach);
	mutex_exit(&dmabuf->db_lock);
	if (ret)
		goto fail0;

	if (attach->dynamic_mapping != dmabuf->ops->dynamic_mapping)
		panic("%s: NYI", __func__);

	return attach;

fail0:	kmem_free(attach, sizeof(*attach));
	return ERR_PTR(ret);
}

struct dma_buf_attachment *
dma_buf_attach(struct dma_buf *dmabuf, bus_dma_tag_t dmat)
{

	return dma_buf_dynamic_attach(dmabuf, dmat, /*dynamic_mapping*/false);
}

void
dma_buf_detach(struct dma_buf *dmabuf, struct dma_buf_attachment *attach)
{

	mutex_enter(&dmabuf->db_lock);
	if (dmabuf->ops->detach)
		dmabuf->ops->detach(dmabuf, attach);
	mutex_exit(&dmabuf->db_lock);

	kmem_free(attach, sizeof(*attach));
}

struct sg_table *
dma_buf_map_attachment(struct dma_buf_attachment *attach,
    enum dma_data_direction dir)
{
	struct sg_table *sg;

	if (attach->dmabuf->ops->dynamic_mapping)
		dma_resv_lock(attach->dmabuf->resv, NULL);
	sg = attach->dmabuf->ops->map_dma_buf(attach, dir);
	if (attach->dmabuf->ops->dynamic_mapping)
		dma_resv_unlock(attach->dmabuf->resv);

	return sg;
}

void
dma_buf_unmap_attachment(struct dma_buf_attachment *attach,
    struct sg_table *sg, enum dma_data_direction dir)
{

	if (attach->dmabuf->ops->dynamic_mapping)
		dma_resv_lock(attach->dmabuf->resv, NULL);
	attach->dmabuf->ops->unmap_dma_buf(attach, sg, dir);
	if (attach->dmabuf->ops->dynamic_mapping)
		dma_resv_unlock(attach->dmabuf->resv);
}

static int
dmabuf_fop_close(struct file *file)
{
	struct dma_buf *dmabuf = file->f_data;

	dma_buf_put(dmabuf);
	return 0;
}

static int
dmabuf_fop_poll(struct file *file, int events)
{
	struct dma_buf *dmabuf = file->f_data;
	struct dma_resv_poll *rpoll = &dmabuf->db_resv_poll;

	return dma_resv_do_poll(dmabuf->resv, events, rpoll);
}

static int
dmabuf_fop_kqfilter(struct file *file, struct knote *kn)
{
	struct dma_buf *dmabuf = file->f_data;
	struct dma_resv_poll *rpoll = &dmabuf->db_resv_poll;

	return dma_resv_kqfilter(dmabuf->resv, kn, rpoll);
}

static int
dmabuf_fop_mmap(struct file *file, off_t *offp, size_t size, int prot,
    int *flagsp, int *advicep, struct uvm_object **uobjp, int *maxprotp)
{
	struct dma_buf *dmabuf = file->f_data;
	int allowed = 0, maxprot, error;

	if (size > dmabuf->size || *offp < 0 || *offp > dmabuf->size - size)
		return EINVAL;
	if (prot & ~(VM_PROT_READ | VM_PROT_WRITE))
		return EINVAL;
	if (file->f_flag & FREAD)
		allowed |= VM_PROT_READ;
	if (file->f_flag & FWRITE)
		allowed |= VM_PROT_WRITE;
	if (prot & ~allowed)
		return EACCES;

	/* DRM callbacks use negative Linux errno; fileops use native errno. */
	maxprot = allowed;
	error = -dmabuf->ops->mmap(dmabuf, offp, size, prot, flagsp, advicep,
	    uobjp, &maxprot);
	if (error == 0) {
		/* A provider cannot grant later mprotect access beyond this fd. */
		*maxprotp = maxprot & allowed;
	}
	return error;
}

/*
 * We don't actually do anything with the file offset; this is just how
 * libdrm_amdgpu expects to find the size of the DMA buf.  (Why it
 * doesn't use fstat is unclear, but it doesn't really matter.)
 */
static int
dmabuf_fop_seek(struct file *fp, off_t delta, int whence, off_t *newoffp,
    int flags)
{
	const off_t OFF_MAX = __type_max(off_t);
	struct dma_buf *dmabuf = fp->f_data;
	off_t base, newoff;
	int error;

	mutex_enter(&fp->f_lock);

	switch (whence) {
	case SEEK_CUR:
		base = fp->f_offset;
		break;
	case SEEK_END:
		base = dmabuf->size;
		break;
	case SEEK_SET:
		base = 0;
		break;
	default:
		error = EINVAL;
		goto out;
	}

	/* Check for arithmetic overflow and reject negative offsets.  */
	if (base < 0 || delta > OFF_MAX - base || base + delta < 0) {
		error = EINVAL;
		goto out;
	}

	/* Compute the new offset.  */
	newoff = base + delta;

	/* Success!  */
	if (newoffp)
		*newoffp = newoff;
	if (flags & FOF_UPDATE_OFFSET)
		fp->f_offset = newoff;
	error = 0;

out:	mutex_exit(&fp->f_lock);
	return error;
}
