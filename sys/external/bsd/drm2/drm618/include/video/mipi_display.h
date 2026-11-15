/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: dist618/drm/i915/display/intel_dsi_dcs_backlight.c (the
 * commands it names); the MIPI DCS specification for the command numbers; the GPL original
 * was not read.
 *
 * Commands of the MIPI Display Command Set that the i915 DSI backlight code uses.
 */
#ifndef _DRM618_VIDEO_MIPI_DISPLAY_H_
#define _DRM618_VIDEO_MIPI_DISPLAY_H_

#define	MIPI_DCS_SET_DISPLAY_BRIGHTNESS		0x51
#define	MIPI_DCS_GET_DISPLAY_BRIGHTNESS		0x52
#define	MIPI_DCS_WRITE_CONTROL_DISPLAY		0x53
#define	MIPI_DCS_GET_CONTROL_DISPLAY		0x54
#define	MIPI_DCS_WRITE_POWER_SAVE		0x55
#define	MIPI_DCS_GET_POWER_SAVE			0x56

#endif
