/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: virtgpu_drv.c of dist618 (MIT),
 * ld_virtio.c of NetBSD (the module and ioconf pattern),
 * drm618/drm618_module.c.
 *
 * Module glue of the virtio-gpu driver of Linux 6.18: what Linux does by
 * module_init (register the virtio driver; drm618_prefix.h turns it into the
 * function drm618_modinit_virtio_gpu_driver_init) and the attach of the
 * autoconf glue (drm618_virtio.c) to the virtio devices.  The unload is
 * refused while a client has a DRM device of the driver open.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/device.h>
#include <sys/module.h>
#include <sys/systm.h>

#include <linux/atomic.h>
#include <linux/virtio.h>

#include <drm/drm_device.h>

#include <drm618_virtio.h>

/* the configuration data, generated from the .ioconf of the module */
#include "ioconf.c"

MODULE(MODULE_CLASS_DRIVER, virtiogpu618, "drmkms618");

int	drm618_modinit_virtio_gpu_driver_init(void);
void	drm618_modexit_virtio_gpu_driver_exit(void);

/* A device whose DRM device has a client is busy. */
static int
virtiogpu618_busy(struct virtio_device *vdev, void *arg __unused)
{
	struct drm_device *dev = vdev->priv;

	if (dev != NULL && atomic_read(&dev->open_count) > 0) {
		printf("virtiogpu618: in use (%d open), not detaching\n",
		    atomic_read(&dev->open_count));
		return EBUSY;
	}
	return 0;
}

static int
virtiogpu618_modcmd(modcmd_t cmd, void *arg __unused)
{
	int error;

	switch (cmd) {
	case MODULE_CMD_INIT:
		error = drm618_modinit_virtio_gpu_driver_init();
		if (error != 0)
			return -error;
		error = config_init_component(cfdriver_ioconf_virtiogpu618,
		    cfattach_ioconf_virtiogpu618, cfdata_ioconf_virtiogpu618);
		if (error != 0)
			drm618_modexit_virtio_gpu_driver_exit();
		return error;
	case MODULE_CMD_FINI:
		error = drm618_virtio_busy(virtiogpu618_busy, NULL);
		if (error != 0)
			return error;
		error = config_fini_component(cfdriver_ioconf_virtiogpu618,
		    cfattach_ioconf_virtiogpu618, cfdata_ioconf_virtiogpu618);
		if (error != 0)
			return error;
		drm618_modexit_virtio_gpu_driver_exit();
		return 0;
	default:
		return ENOTTY;
	}
}
