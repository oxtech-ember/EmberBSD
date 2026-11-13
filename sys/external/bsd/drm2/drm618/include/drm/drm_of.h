/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD clean-room implementation of the interface used by
 * MIT-licensed consumers; written without access to the GPL original.
 *
 * Device-tree helpers of the DRM core.  EmberBSD has no device tree on the
 * platforms this core runs on, so the helpers report that there is none.
 */

#ifndef _DRM618_DRM_OF_H_
#define _DRM618_DRM_OF_H_

#include <linux/errno.h>

struct device_node;

/* Data mapping of an LVDS panel from its device-tree node: a negative errno. */
static inline int
drm_of_lvds_get_data_mapping(const struct device_node *port)
{

	return -ENODEV;
}

#endif	/* _DRM618_DRM_OF_H_ */
