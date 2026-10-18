/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _ARM_BROADCOM_BCM2712_DISPLAY_HDMI0VAR_H_
#define _ARM_BROADCOM_BCM2712_DISPLAY_HDMI0VAR_H_

#include <sys/types.h>

/* Future RMW value: (old & ~clear_mask) | set_bits. Overlap is permitted. */
struct bcm2712_hdmi0_update {
	uint32_t clear_mask;
	uint32_t set_bits;
};

/* Named payloads, not a register-write interface or an application order. */
struct bcm2712_hdmi0_timing {
	uint32_t horza;
	uint32_t horzb;
	uint32_t verta0;
	uint32_t verta1;
	uint32_t vertb0;
	uint32_t vertb1;
	struct bcm2712_hdmi0_update scheduler;
	struct bcm2712_hdmi0_update deep_color;
	struct bcm2712_hdmi0_update gcp_word;
	struct bcm2712_hdmi0_update gcp_config;
	struct bcm2712_hdmi0_update misc_control;
	uint32_t clock_stop;
};

/*
 * Encode progressive 1920x1080@60, positive syncs, RGB8, no pixel repetition.
 * NULL returns EINVAL. A valid output receives the complete fixed payload.
 * No I/O, state, callback, sink policy or lifecycle validation occurs.
 * GCP and CLOCK_STOP are future commands, not evidence of inactive hardware.
 */
int bcm2712_hdmi0_encode_1080p60(struct bcm2712_hdmi0_timing *);

#endif /* _ARM_BROADCOM_BCM2712_DISPLAY_HDMI0VAR_H_ */
