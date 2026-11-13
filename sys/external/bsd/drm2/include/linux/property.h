/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD.
 *
 * Device properties (fwnode) of Linux.  NetBSD's device_t has no firmware
 * node: the functions say so (no node, no property), as Linux does for a
 * device that was not described by firmware.  A port that wants properties
 * of ACPI or Device Tree devices implements these on device_t.
 */

#ifndef _LINUX_PROPERTY_H_
#define _LINUX_PROPERTY_H_

#include <sys/types.h>

#include <linux/err.h>
#include <linux/errno.h>

struct device;
/* The one member the DRM core reads: the node a device was merged into. */
struct fwnode_handle {
	struct fwnode_handle	*secondary;
};

static inline struct fwnode_handle *
dev_fwnode(const struct device *dev)
{
	return NULL;
}

static inline bool
fwnode_device_is_available(const struct fwnode_handle *fwnode)
{
	return false;
}

static inline void
fwnode_handle_put(struct fwnode_handle *fwnode)
{
}

static inline struct fwnode_handle *
fwnode_find_reference(const struct fwnode_handle *fwnode, const char *name,
    unsigned int index)
{
	return ERR_PTR(-ENOENT);
}

static inline bool
device_property_present(const struct device *dev, const char *propname)
{
	return false;
}

static inline int
device_property_read_u32(const struct device *dev, const char *propname,
    uint32_t *val)
{
	return -EINVAL;
}

static inline bool
device_property_read_bool(const struct device *dev, const char *propname)
{
	return false;
}

#endif	/* _LINUX_PROPERTY_H_ */
