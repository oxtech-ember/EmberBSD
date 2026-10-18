/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Numeric facts: raspberrypi/linux a1a601d4857978d810175365550c4536701df4ed,
 * vc4_crtc.c and vc4_regs.h. No GPL implementation code is copied here.
 */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");
#include <sys/param.h>
#include <sys/errno.h>
#include <arm/broadcom/bcm2712_display_pv0var.h>

#define PV0_PPC			1U
#define PV0_CLOCK_SELECT	0U

int
bcm2712_pv0_encode_1080p60(struct bcm2712_pv0_timing *out)
{
	const struct bcm2712_pv0_timing result = {
		.control = ((64U - 3U * 6U) << 15) |
		    (1U << 14) | (1U << 13) | (1U << 12) |
		    (PV0_CLOCK_SELECT << 2) | (1U << 1),
		.v_control = (1U << 29) | (1U << 1),
		.vsyncd_even = 0,
		.horza = ((148U / PV0_PPC) << 16) | (44U / PV0_PPC),
		.horzb = ((88U / PV0_PPC) << 16) | (1920U / PV0_PPC),
		.verta = (36U << 16) | 5U,
		.vertb = (4U << 16) | 1080U,
		.mux_cfg = 8U << 2,
		.pipe_init_ctrl = (1U << 8) | (1U << 4) | 1U,
	};

	if (out == NULL)
		return EINVAL;
	*out = result;
	return 0;
}
