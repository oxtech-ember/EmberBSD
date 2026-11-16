/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation; written to the calls of drm618_virtio.c and
 * drm618_virtiogpu_module.c, no Linux code.
 *
 * What the module glue of a virtio driver of the 6.18 core asks of the
 * virtio layer beyond the Linux interface.
 */

#ifndef _DRM618_VIRTIO_H_
#define _DRM618_VIRTIO_H_

struct virtio_device;

/*
 * Ask busy about every device whose driver was probed; the first non-zero
 * answer ends the walk and is returned (an unload that must not take a
 * device with open files).
 */
int	drm618_virtio_busy(int (*)(struct virtio_device *, void *), void *);
void	drm618_virtio_fini(void);	/* before the module goes: the lock of the driver table */

/*
 * Set by the driver module: asked by the detach of a device (not forced)
 * whether it has clients; non-zero refuses the detach.
 */
extern int (*drm618_virtio_busy_fn)(struct virtio_device *, void *);

#endif	/* _DRM618_VIRTIO_H_ */
