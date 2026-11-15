/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: virtgpu_drv.h, virtgpu_kms.c,
 * virtgpu_vq.c of dist618 (MIT); the virtio specification for the feature
 * numbers (VIRTIO_F_VERSION_1 is bit 32, VIRTIO_F_ACCESS_PLATFORM bit 33);
 * the GPL headers of Linux were not read.
 *
 * Shadow of <linux/virtio_config.h> for the 6.18 core: the feature bit
 * numbers (Linux takes numbers, NetBSD's virtioreg.h masks: this header must
 * not be read together with dev/pci/virtioreg.h) and the access to the device
 * configuration space.
 */

#ifndef _DRM618_LINUX_VIRTIO_CONFIG_H_
#define _DRM618_LINUX_VIRTIO_CONFIG_H_

#include <sys/types.h>
#include <sys/stddef.h>

#include <linux/virtio.h>

#define	VIRTIO_F_VERSION_1		32
#define	VIRTIO_F_ACCESS_PLATFORM	33

#define	virtio_cread_le(vdev, structname, member, ptr)	do {		\
	*(ptr) = (__typeof__(*(ptr)))drm618_virtio_cread((vdev),	\
	    offsetof(structname, member), sizeof(*(ptr)));		\
} while (0)

#define	virtio_cwrite_le(vdev, structname, member, ptr)	do {		\
	drm618_virtio_cwrite((vdev), offsetof(structname, member),	\
	    sizeof(*(ptr)), (u64)*(ptr));				\
} while (0)

#endif	/* _DRM618_LINUX_VIRTIO_CONFIG_H_ */
