/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD clean-room implementation of the interface used by
 * MIT-licensed consumers; written without access to the GPL original.
 *
 * Accelerator device nodes (/dev/accel).  EmberBSD builds the core without
 * accelerator support (CONFIG_DRM_ACCEL is off), so only the entry points
 * drm_drv.c calls remain, as no-ops.
 */

#ifndef _DRM618_DRM_ACCEL_H_
#define _DRM618_DRM_ACCEL_H_

#include <linux/xarray.h>

struct drm_device;

/* Highest minor number of an accelerator node (inclusive). */
#define ACCEL_MAX_MINORS	255

#if IS_ENABLED(CONFIG_DRM_ACCEL)

extern struct xarray accel_minors_xa;

int accel_core_init(void);
void accel_core_exit(void);
void accel_debugfs_register(struct drm_device *dev);

#else

static inline int
accel_core_init(void)
{

	return 0;
}

static inline void
accel_core_exit(void)
{
}

static inline void
accel_debugfs_register(struct drm_device *dev)
{
}

#endif

#endif	/* _DRM618_DRM_ACCEL_H_ */
