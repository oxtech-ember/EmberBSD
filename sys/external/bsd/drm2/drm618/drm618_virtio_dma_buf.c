/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: virtgpu_prime.c, virtgpu_plane.c of
 * dist618 (MIT), drm618/include/linux/virtio_dma_buf.h; the GPL original was
 * not read.
 *
 * A virtio dma-buf is one whose exporter's operations are a
 * struct virtio_dma_buf_ops: recognised by the attach function.
 */

#include <sys/cdefs.h>

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/virtio_dma_buf.h>

bool
is_virtio_dma_buf(struct dma_buf *dma_buf)
{

	return dma_buf->ops->attach == virtio_dma_buf_attach;
}

int
virtio_dma_buf_attach(struct dma_buf *dma_buf, struct dma_buf_attachment *attach)
{
	const struct virtio_dma_buf_ops *ops;

	if (!is_virtio_dma_buf(dma_buf))
		return -EINVAL;
	ops = (const struct virtio_dma_buf_ops *)dma_buf->ops;
	if (ops->device_attach == NULL)
		return 0;
	return ops->device_attach(dma_buf, attach);
}

struct dma_buf *
virtio_dma_buf_export(const struct dma_buf_export_info *exp_info)
{

	if (exp_info->ops == NULL || exp_info->ops->attach != virtio_dma_buf_attach)
		return ERR_PTR(-EINVAL);
	return dma_buf_export(exp_info);
}

int
virtio_dma_buf_get_uuid(struct dma_buf *dma_buf, uuid_t *uuid)
{
	const struct virtio_dma_buf_ops *ops;

	if (!is_virtio_dma_buf(dma_buf))
		return -EINVAL;
	ops = (const struct virtio_dma_buf_ops *)dma_buf->ops;
	if (ops->get_uuid == NULL)
		return -EINVAL;
	return ops->get_uuid(dma_buf, uuid);
}
