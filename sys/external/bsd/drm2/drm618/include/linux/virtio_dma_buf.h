/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: virtgpu_prime.c, virtgpu_plane.c of
 * dist618 (MIT), drm618/include/linux/dma-buf.h; the GPL header of Linux was
 * not read.
 *
 * dma-bufs that can say which virtio resource they are: an exporter's
 * operations with two more members, the UUID of the resource and the
 * attachment of the exporting device.  Implementation: drm618_virtio_dma_buf.c.
 */

#ifndef _DRM618_LINUX_VIRTIO_DMA_BUF_H_
#define _DRM618_LINUX_VIRTIO_DMA_BUF_H_

#include <linux/dma-buf.h>
#include <linux/uuid.h>

struct virtio_dma_buf_ops {
	struct dma_buf_ops	ops;
	/* The attach of the exporting device (what ops.attach is for others). */
	int	(*device_attach)(struct dma_buf *, struct dma_buf_attachment *);
	int	(*get_uuid)(struct dma_buf *, uuid_t *);
};

/* Use as ops.attach of the exporter; runs device_attach. */
int	virtio_dma_buf_attach(struct dma_buf *, struct dma_buf_attachment *);

/* dma_buf_export for an exporter whose ops are a virtio_dma_buf_ops (-EINVAL otherwise). */
struct dma_buf *virtio_dma_buf_export(const struct dma_buf_export_info *);

bool	is_virtio_dma_buf(struct dma_buf *);
/* The UUID of the resource behind the buffer: -EINVAL if it is not a virtio one. */
int	virtio_dma_buf_get_uuid(struct dma_buf *, uuid_t *);

#endif	/* _DRM618_LINUX_VIRTIO_DMA_BUF_H_ */
