/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: dist618/drm/i915/i915_driver.c (the members of
 * i915_pm_ops); the GPL original was not read.
 *
 * Shadow of the shared <linux/pm.h> for the 6.18 sources: the table of power
 * management operations has the prepare and complete steps too.  The shared struct (for
 * the 5.6 drivers) is renamed.
 */
#ifndef _DRM618_LINUX_PM_H_
#define _DRM618_LINUX_PM_H_

#define	dev_pm_ops	drm618_shared_dev_pm_ops
#include_next <linux/pm.h>
#undef	dev_pm_ops

struct dev_pm_ops {
	int	(*prepare)(struct device *);
	void	(*complete)(struct device *);
	int	(*resume)(struct device *);
	int	(*resume_early)(struct device *);
	int	(*suspend)(struct device *);
	int	(*suspend_late)(struct device *);
	int	(*freeze)(struct device *);
	int	(*freeze_late)(struct device *);
	int	(*thaw_early)(struct device *);
	int	(*thaw)(struct device *);
	int	(*poweroff)(struct device *);
	int	(*poweroff_late)(struct device *);
	int	(*restore_early)(struct device *);
	int	(*restore)(struct device *);
	int	(*runtime_suspend)(struct device *);
	int	(*runtime_resume)(struct device *);
	int	(*runtime_idle)(struct device *);
};

#endif
