/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD, AI-assisted. Independent A733 TV0 timing encoding.
 * Hardware facts: ember/boot/a733-output-timing.md. No GPL code is imported.
 */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <arm/sunxi/sun60i_a733_output.h>

int
sun60i_a733_tcon_timing_encode(const struct sun60i_a733_output_mode *mode,
    struct sun60i_a733_tcon_timing *out)
{
	struct sun60i_a733_tcon_timing value = { 0 };
	uint32_t hback, vback, hsync, vsync;

	if (mode == NULL || out == NULL)
		return EINVAL;
	if (mode->pixel_clock_hz == 0 || mode->hdisplay == 0 ||
	    mode->vdisplay == 0 || mode->hdisplay >= mode->hsync_start ||
	    mode->hsync_start >= mode->hsync_end ||
	    mode->hsync_end >= mode->htotal ||
	    mode->vdisplay >= mode->vsync_start ||
	    mode->vsync_start >= mode->vsync_end ||
	    mode->vsync_end >= mode->vtotal)
		return EINVAL;
	/* These are TV0 field limits, not an admitted HDMI or DE mode. */
	if (mode->pixel_clock_hz > 600000000 || mode->hdisplay > 32768 ||
	    mode->vdisplay > 32768 || mode->htotal > 65536 ||
	    mode->vtotal > 65535)
		return ERANGE;
	hback = mode->htotal - mode->hsync_start - 1;
	vback = mode->vtotal - mode->vsync_start - 1;
	hsync = mode->hsync_end - mode->hsync_start - 1;
	vsync = mode->vsync_end - mode->vsync_start - 1;
	if (hback > 32767 || vback > 32767 || hsync > 4095 || vsync > 4095)
		return ERANGE;

	/* A733 places doubled progressive VT at 0x98, not in BASIC4. */
	value.basic1 = mode->vtotal * 2;
	value.basic2 = ((mode->hdisplay - 1) << 16) | (mode->vdisplay - 1);
	value.basic3 = ((mode->htotal - 1) << 16) | hback;
	value.basic4 = vback;
	value.basic5 = (hsync << 16) | vsync;
	if (mode->hsync_positive)
		value.polarity |= UINT32_C(0x02000000);
	if (mode->vsync_positive)
		value.polarity |= UINT32_C(0x01000000);
	*out = value;
	return 0;
}

int
sun60i_a733_tcon_vblank_ack(uint32_t snapshot, uint32_t *out)
{
	const uint32_t enable = UINT32_C(0x40000000);
	const uint32_t pending = UINT32_C(0x00004000);

	if (out == NULL)
		return EINVAL;
	if ((snapshot & (enable | pending)) != (enable | pending))
		return ENOENT;
	/*
	 * Preserve all known RW enables. Write one to other W0C flags, including
	 * events that can arrive after the snapshot. Reserved bits remain zero.
	 */
	*out = (snapshot & UINT32_C(0x5f000000)) | UINT32_C(0x00001f00);
	return 0;
}
