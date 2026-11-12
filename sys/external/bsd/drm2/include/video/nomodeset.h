/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD native. Interface of Linux 6.18 include/video/nomodeset.h (GPL-2.0).  The layer
 * has no nomodeset switch, so mode-setting drivers are never restricted to
 * firmware framebuffers.
 */

#ifndef _VIDEO_NOMODESET_H_
#define _VIDEO_NOMODESET_H_

#include <linux/types.h>

static inline bool
video_firmware_drivers_only(void)
{

	return false;
}

#endif	/* _VIDEO_NOMODESET_H_ */
