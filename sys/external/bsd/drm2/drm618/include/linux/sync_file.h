/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD.  Shadow of the shared
 * <linux/sync_file.h> for the 6.18 core: a sync file is a Linux file
 * (drm618_sync_file.c).
 */
#ifndef _DRM618_LINUX_SYNC_FILE_H_
#define _DRM618_LINUX_SYNC_FILE_H_

#include <drm618_file.h>
#include <linux/dma-fence.h>
#include <linux/wait.h>

struct sync_file {
	struct file		*file;
	struct dma_fence	*fence;
	struct dma_fence_cb	cb;
	wait_queue_head_t	wq;
	bool			signalled;
};

struct sync_file *sync_file_create(struct dma_fence *);
struct dma_fence *sync_file_get_fence(int);

#endif
