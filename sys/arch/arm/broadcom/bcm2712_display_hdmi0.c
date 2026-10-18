/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Numeric facts: raspberrypi/linux a1a601d4857978d810175365550c4536701df4ed,
 * vc4_hdmi.c and vc4_hdmi_regs.h. No GPL implementation code is copied here.
 */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");
#include <sys/param.h>
#include <sys/errno.h>
#include <arm/broadcom/bcm2712_display_hdmi0var.h>

int
bcm2712_hdmi0_encode_1080p60(struct bcm2712_hdmi0_timing *out)
{
	const struct bcm2712_hdmi0_timing result = {
		.horza = (88U << 16) | (1U << 15) | (1U << 14) | 1920U,
		.horzb = (148U << 16) | 44U,
		.verta0 = (5U << 24) | (4U << 16) | 1080U,
		.verta1 = (5U << 24) | (4U << 16) | 1080U,
		.vertb0 = 36U,
		.vertb1 = ((2200U / 2U) << 16) | 36U,
		.scheduler = {
			.clear_mask = 0,
			.set_bits = (1U << 15) | (1U << 5),
		},
		.deep_color = {
			.clear_mask = (7U << 8) | 15U,
			.set_bits = 2U << 8,
		},
		.gcp_word = {
			.clear_mask = 0xffffU,
			.set_bits = 1U << 4,
		},
		.gcp_config = {
			.clear_mask = 0,
			.set_bits = 1U << 31,
		},
		.misc_control = { .clear_mask = 15U, .set_bits = 0 },
		.clock_stop = 0,
	};

	if (out == NULL)
		return EINVAL;
	*out = result;
	return 0;
}
