/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: virtgpu_drv.h, virtgpu_ioctl.c,
 * virtgpu_object.c, virtgpu_prime.c, virtgpu_plane.c of dist618 (MIT) for the
 * four functions and their callers; the original virtgpu_vram.c was not
 * imported (it carries no MIT licence text) and not read.
 *
 * Host visible blob resources ("vram": memory of the device mapped into the
 * guest) need the shared memory region of the virtio device, which
 * virtio_get_shm_region does not provide (drm618_virtio.c).  No object is ever
 * of this kind: the creation is refused with a message, the tests for such an
 * object say no.
 */

#include <sys/cdefs.h>

#include <linux/err.h>
#include <linux/errno.h>

#include "virtgpu_drv.h"

bool
virtio_gpu_is_vram(struct virtio_gpu_object *bo __unused)
{

	return false;
}

int
virtio_gpu_vram_create(struct virtio_gpu_device *vgdev,
    struct virtio_gpu_object_params *params __unused,
    struct virtio_gpu_object **bo_ptr)
{

	*bo_ptr = NULL;
	DRM_ERROR("host visible blob resources are not supported on this"
	    " system (no shared memory region of the device)\n");
	(void)vgdev;
	return -EOPNOTSUPP;
}

struct sg_table *
virtio_gpu_vram_map_dma_buf(struct virtio_gpu_object *bo __unused,
    struct device *dev __unused, enum dma_data_direction dir __unused)
{

	return ERR_PTR(-EOPNOTSUPP);
}

void
virtio_gpu_vram_unmap_dma_buf(struct device *dev __unused,
    struct sg_table *sgt __unused, enum dma_data_direction dir __unused)
{
}
