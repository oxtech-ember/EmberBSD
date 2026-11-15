/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: dist618/drm/i915/gt/uc/intel_huc.c (the use of the aux
 * device of the GSC and of the notifier of the bus); dist618/drm/i915/gt/intel_gsc.c (the
 * members it fills in); the GPL original was not read.
 *
 * The MEI auxiliary device of the GSC, as the HuC code names it.  There is no MEI and no auxiliary bus:
 * the types are here so that the code compiles, the calls are declared, not defined.  A kernel
 * with this code and no MEI cannot load the HuC through the GSC, nor use HDCP and PXP that
 * go through it.
 */
#ifndef _DRM618_LINUX_MEI_AUX_H_
#define _DRM618_LINUX_MEI_AUX_H_

#include <sys/device.h>

#include <linux/ioport.h>
#include <linux/notifier.h>
#include <linux/types.h>

struct bus_type;

struct auxiliary_device {
	const char	*name;
	u32		id;
};

struct mei_aux_device {
	struct auxiliary_device	aux_dev;
	int			irq;
	struct resource		bar;
	struct resource		ext_op_mem;
	bool			slow_firmware;
};

#define	BUS_NOTIFY_ADD_DEVICE		0x00000001
#define	BUS_NOTIFY_DEL_DEVICE		0x00000002
#define	BUS_NOTIFY_BOUND_DRIVER		0x00000005
#define	BUS_NOTIFY_UNBIND_DRIVER	0x00000006
#define	BUS_NOTIFY_DRIVER_NOT_BOUND	0x00000008

/* The struct device of the auxiliary device (declared, not defined: there is no auxiliary bus; struct device is opaque here). */
struct device *drm618_aux_device_of(struct mei_aux_device *);

int	bus_register_notifier(const struct bus_type *, struct notifier_block *);
int	bus_unregister_notifier(const struct bus_type *, struct notifier_block *);

#endif
