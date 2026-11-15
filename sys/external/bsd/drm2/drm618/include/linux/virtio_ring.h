/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: virtgpu_kms.c of dist618 (MIT); the
 * virtio specification for the feature number (indirect descriptors are bit
 * 28); the GPL headers of Linux were not read.
 *
 * Shadow of <linux/virtio_ring.h> for the 6.18 core: the feature number the
 * driver tests.  The ring itself belongs to NetBSD.
 */

#ifndef _DRM618_LINUX_VIRTIO_RING_H_
#define _DRM618_LINUX_VIRTIO_RING_H_

#include <linux/virtio.h>

#define	VIRTIO_RING_F_INDIRECT_DESC	28

#endif	/* _DRM618_LINUX_VIRTIO_RING_H_ */
