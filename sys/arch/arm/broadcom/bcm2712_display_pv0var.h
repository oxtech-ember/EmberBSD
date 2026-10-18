/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _ARM_BROADCOM_BCM2712_DISPLAY_PV0VAR_H_
#define _ARM_BROADCOM_BCM2712_DISPLAY_PV0VAR_H_

#include <sys/types.h>

/* Fixed BCM2712 PV0 payload. Fields do not define an application order. */
struct bcm2712_pv0_timing {
	uint32_t control;
	uint32_t v_control;
	uint32_t vsyncd_even;
	uint32_t horza;
	uint32_t horzb;
	uint32_t verta;
	uint32_t vertb;
	uint32_t mux_cfg;
	uint32_t pipe_init_ctrl;
};

/*
 * Encode one progressive 1920x1080@60 RGB8 profile. NULL returns EINVAL.
 * A valid output receives all fields. No I/O or lifecycle operation occurs.
 * CONTROL contains FIFO_CLR and PIPE_INIT_CTRL contains pipe-init commands.
 * These are future configuration payloads that require caller-owned quiesce.
 * PV enable and video enable remain clear. This API does not apply the payload.
 */
int bcm2712_pv0_encode_1080p60(struct bcm2712_pv0_timing *);

#endif /* _ARM_BROADCOM_BCM2712_DISPLAY_PV0VAR_H_ */
