/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD native.  Interface of Linux 6.18 arch/x86/include/asm/video.h
 * (GPL-2.0; no source copied).
 *
 * Only the boot display query used by drm_sysfs.  The layer has no way to
 * tell which PCI device the firmware used for the console, so no device is
 * reported as the primary one (the boot_display sysfs attribute is not
 * created).
 */

#ifndef _ASM_VIDEO_H_
#define _ASM_VIDEO_H_

#include <linux/types.h>

struct device;

static inline bool
video_is_primary_device(struct device *dev __unused)
{

	return false;
}

#endif	/* _ASM_VIDEO_H_ */
