/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD native.  Interface of Linux 6.18 include/video/cmdline.h
 * (GPL-2.0; no source copied).
 *
 * The video= kernel command line option is not supported: no connector has
 * a command-line mode override.
 */

#ifndef _VIDEO_CMDLINE_H_
#define _VIDEO_CMDLINE_H_

#include <linux/types.h>

static inline const char *
video_get_options(const char *name __unused)
{

	return NULL;
}

#endif	/* _VIDEO_CMDLINE_H_ */
