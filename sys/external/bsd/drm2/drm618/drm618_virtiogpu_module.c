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
#include <sys/sysctl.h>
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

extern u_int drm618_virtio_leaked;

#if defined(DRM618_VIRTIO_FAULT_INJECT) || defined(DRM618_VIRTIO_STATS)
/* A test build only (make VIRTIO_TEST=1): fault injection and counters as sysctl nodes. */
static struct sysctllog *virtiogpu618_log;

static void
virtiogpu618_sysctl(void)
{
	const struct sysctlnode *node = NULL;

	if (sysctl_createv(&virtiogpu618_log, 0, NULL, &node, CTLFLAG_PERMANENT,
	    CTLTYPE_NODE, "virtiogpudrm", NULL, NULL, 0, NULL, 0, CTL_HW,
	    CTL_CREATE, CTL_EOL) != 0)
		return;
#ifdef DRM618_VIRTIO_FAULT_INJECT
	{
		extern int drm618_virtio_drop_notify;

		sysctl_createv(&virtiogpu618_log, 0, &node, NULL, CTLFLAG_READWRITE,
		    CTLTYPE_INT, "drop_notify", SYSCTL_DESCR("do not notify the device of commands"),
		    NULL, 0, &drm618_virtio_drop_notify, 0, CTL_CREATE, CTL_EOL);
	}
#endif
#ifdef DRM618_VIRTIO_STATS
	{
		extern u_int drm618_virtio_n_reqs, drm618_virtio_n_reqs_freed;
		extern u_int drm618_virtio_n_maps, drm618_virtio_n_maps_freed;

		sysctl_createv(&virtiogpu618_log, 0, &node, NULL, CTLFLAG_READONLY,
		    CTLTYPE_INT, "commands", SYSCTL_DESCR("commands queued"),
		    NULL, 0, (int *)&drm618_virtio_n_reqs, 0, CTL_CREATE, CTL_EOL);
		sysctl_createv(&virtiogpu618_log, 0, &node, NULL, CTLFLAG_READONLY,
		    CTLTYPE_INT, "commands_done", SYSCTL_DESCR("commands completed or dropped"),
		    NULL, 0, (int *)&drm618_virtio_n_reqs_freed, 0, CTL_CREATE, CTL_EOL);
		sysctl_createv(&virtiogpu618_log, 0, &node, NULL, CTLFLAG_READONLY,
		    CTLTYPE_INT, "dmamaps", SYSCTL_DESCR("DMA maps made"),
		    NULL, 0, (int *)&drm618_virtio_n_maps, 0, CTL_CREATE, CTL_EOL);
		sysctl_createv(&virtiogpu618_log, 0, &node, NULL, CTLFLAG_READONLY,
		    CTLTYPE_INT, "dmamaps_freed", SYSCTL_DESCR("DMA maps destroyed"),
		    NULL, 0, (int *)&drm618_virtio_n_maps_freed, 0, CTL_CREATE, CTL_EOL);
	}
#endif
}
#define	VGPU_SYSCTL_SETUP()		virtiogpu618_sysctl()
#define	VGPU_SYSCTL_TEARDOWN()	sysctl_teardown(&virtiogpu618_log)
#else
#define	VGPU_SYSCTL_SETUP()		((void)0)
#define	VGPU_SYSCTL_TEARDOWN()	((void)0)
#endif

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
		if (error != 0) {
			drm618_virtio_fini();	/* the table of drivers may have set up its lock */
			return -error;
		}
		VGPU_SYSCTL_SETUP();
		drm618_virtio_busy_fn = virtiogpu618_busy;
		error = config_init_component(cfdriver_ioconf_virtiogpu618,
		    cfattach_ioconf_virtiogpu618, cfdata_ioconf_virtiogpu618);
		if (error != 0) {
			drm618_virtio_busy_fn = NULL;
			drm618_modexit_virtio_gpu_driver_exit();
			VGPU_SYSCTL_TEARDOWN();
			drm618_virtio_fini();
		}
		return error;
	case MODULE_CMD_FINI:
		if (drm618_virtio_leaked != 0) {
			printf("virtiogpu618: %u queue(s) could not be freed, the"
			    " module stays\n", drm618_virtio_leaked);
			return EBUSY;
		}
		error = drm618_virtio_busy(virtiogpu618_busy, NULL);
		if (error != 0)
			return error;
		error = config_fini_component(cfdriver_ioconf_virtiogpu618,
		    cfattach_ioconf_virtiogpu618, cfdata_ioconf_virtiogpu618);
		if (error != 0)
			return error;
		drm618_virtio_busy_fn = NULL;
		drm618_modexit_virtio_gpu_driver_exit();
		VGPU_SYSCTL_TEARDOWN();
		drm618_virtio_fini();
		return 0;
	default:
		return ENOTTY;
	}
}
