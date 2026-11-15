/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: the uses of pm_runtime_suspended in dist618/drm/i915/intel_runtime_pm.h; the GPL original was not read.
 *
 * The layer has no runtime power management of devices: a device is never
 * suspended behind the driver's back, so it always reads as active.
 */
#ifndef _DRM618_LINUX_PM_RUNTIME_H_
#define _DRM618_LINUX_PM_RUNTIME_H_

#include_next <linux/pm_runtime.h>

static inline bool
pm_runtime_suspended(struct device *dev __unused)
{

	return false;
}

#endif
