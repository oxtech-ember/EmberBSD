/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD; written to the interface the
 * 6.18 DRM core uses (drm_atomic_uapi.c, drm_syncobj.c), no Linux code.
 *
 * A sync file is a Linux file (drm618_file.h) that holds one dma_fence: it
 * becomes readable (poll) when the fence signals.  The 5.6 layer has its own
 * sync file on NetBSD files; this one is for the 6.18 core.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/kmem.h>
#include <sys/systm.h>

#include <linux/dma-fence.h>
#include <linux/file.h>
#include <linux/poll.h>
#include <linux/sync_file.h>
#include <linux/wait.h>

static void
sync_file_cb(struct dma_fence *fence __unused, struct dma_fence_cb *cb)
{
	struct sync_file *sf = container_of(cb, struct sync_file, cb);

	sf->signalled = true;
	wake_up_all(&sf->wq);
}

static unsigned int
sync_file_poll(struct file *filp, struct poll_table_struct *pt)
{
	struct sync_file *sf = filp->private_data;

	poll_wait(filp, &sf->wq, pt);
	return sf->signalled ? EPOLLIN : 0;
}

static int
sync_file_release(struct inode *inode __unused, struct file *filp)
{
	struct sync_file *sf = filp->private_data;

	if (!sf->signalled)
		dma_fence_remove_callback(sf->fence, &sf->cb);
	dma_fence_put(sf->fence);
	destroy_waitqueue_head(&sf->wq);
	kmem_free(sf, sizeof(*sf));
	return 0;
}

static const struct file_operations sync_file_fops = {
	.release = sync_file_release,
	.poll = sync_file_poll,
};

struct sync_file *
sync_file_create(struct dma_fence *fence)
{
	struct sync_file *sf;

	sf = kmem_zalloc(sizeof(*sf), KM_SLEEP);
	init_waitqueue_head(&sf->wq);
	sf->fence = dma_fence_get(fence);
	sf->file = anon_inode_getfile("sync_file", &sync_file_fops, sf, O_RDWR);
	/* A fence that is signalled already gives no callback: the file is readable. */
	if (dma_fence_add_callback(sf->fence, &sf->cb, sync_file_cb) != 0)
		sf->signalled = true;
	return sf;
}

struct dma_fence *
sync_file_get_fence(int fd)
{
	struct file *f = fget(fd);
	struct dma_fence *fence;

	if (f == NULL)
		return NULL;
	if (f->f_op != &sync_file_fops) {
		fput(f);
		return NULL;
	}
	fence = dma_fence_get(((struct sync_file *)f->private_data)->fence);
	fput(f);
	return fence;
}
