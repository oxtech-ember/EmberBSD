/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: the CONFIG_* names that dist618/drm/i915 tests (grep); the
 * Kconfig defaults of the i915 profile as the 5.6 port of this tree set them (i915drm/files.i915drmkms);
 * the GPL original (Kconfig) was not read.
 *
 * Compiled into every file of the i915 driver (-include after drm618_prefix.h).
 * It has two jobs: the Kconfig options of the driver for this build, and the
 * headers that Linux's include chain brings into every i915 file by way of
 * <linux/pci.h> and i915_drv.h (the sources use readl and the like without
 * naming the header).
 *
 * An option that is off stays undefined (the sources test both #ifdef and
 * IS_ENABLED()); the layer's IS_ENABLED is true only for a macro defined as 1.
 */
#ifndef _I915DRM618_PREFIX_H_
#define _I915DRM618_PREFIX_H_

/*
 * Timing options, in milliseconds, of the i915 profile.  Defaults of the
 * driver, except as noted:
 *  REQUEST_TIMEOUT 20000, FENCE_TIMEOUT 10000, HEARTBEAT_INTERVAL 2500,
 *  PREEMPT_TIMEOUT 640 (compute 7500), STOP_TIMEOUT 100,
 *  MAX_REQUEST_BUSYWAIT 8000 (ns), TIMESLICE_DURATION 1,
 *  USERFAULT_AUTOSUSPEND 250.
 */
#define	CONFIG_DRM_I915_REQUEST_TIMEOUT		20000
#define	CONFIG_DRM_I915_FENCE_TIMEOUT		10000
#define	CONFIG_DRM_I915_HEARTBEAT_INTERVAL	2500
#define	CONFIG_DRM_I915_PREEMPT_TIMEOUT		640
#define	CONFIG_DRM_I915_PREEMPT_TIMEOUT_COMPUTE	7500
#define	CONFIG_DRM_I915_STOP_TIMEOUT		100
#define	CONFIG_DRM_I915_MAX_REQUEST_BUSYWAIT	8000
#define	CONFIG_DRM_I915_TIMESLICE_DURATION	1
#define	CONFIG_DRM_I915_USERFAULT_AUTOSUSPEND	250

/* Options that are on. */
#define	CONFIG_PM			1	/* runtime pm calls compile; the layer has none */
#define	CONFIG_64BIT			1
#define	CONFIG_DRM_I915_FORCE_PROBE	""	/* no platform is forced; i915.force_probe sets it at run time */

#include <linux/io.h>		/* readb, writel, ... (intel_uncore.h) */
#include <linux/timekeeping.h>	/* ktime_get_raw_fast_ns */
#include <linux/interrupt.h>	/* struct tasklet_struct (i915_scheduler_types.h) */
#include <linux/kobject.h>	/* struct attribute_group, struct kobject (i915_pmu.h, i915_perf_types.h, gt) */
#include <linux/sysfs.h>
#include <linux/pwm.h>		/* struct pwm_state (intel_display_types.h) */

#endif
