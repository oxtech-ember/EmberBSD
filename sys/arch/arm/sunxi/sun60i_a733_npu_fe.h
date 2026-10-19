/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted; bounded native FE admission. */
#ifndef _SUN60I_A733_NPU_FE_H
#define _SUN60I_A733_NPU_FE_H
#include <sys/bus.h>
struct clk;

enum sun60i_a733_npu_fe_register {
	A733_NPU_FE_CONTROL, A733_NPU_FE_IDLE, A733_NPU_FE_IRQ_ENABLE,
	A733_NPU_FE_REVISION, A733_NPU_FE_PID, A733_NPU_FE_MMU,
	A733_NPU_FE_COMMAND_CONTROL, A733_NPU_FE_PD, A733_NPU_FE_COMMAND_ADDRESS,
	A733_NPU_FE_DMA_ADDRESS, A733_NPU_FE_DMA_LOW, A733_NPU_FE_DMA_HIGH,
	A733_NPU_FE_NREGS
};
#define A733_NPU_FE_EVENT_BYTES 16
#define A733_NPU_FE_RUN_BYTES 40
struct sun60i_a733_npu_fe_snapshot {
	uint32_t words[2][A733_NPU_FE_NREGS];
	bool valid;
};

/* Complete raw data only; valid does not imply admission or execution. */
int sun60i_a733_npu_fe_snapshot(bus_space_tag_t, struct clk *, int,
    const void *, struct sun60i_a733_npu_fe_snapshot *);
/* Pure finite-profile check. This does not replace a fresh resource proof. */
int sun60i_a733_npu_fe_admit(const struct sun60i_a733_npu_fe_snapshot *);
/* Exact event4/block6 + END bytes. No command execution or semantic proof. */
int sun60i_a733_npu_fe_event_image(void *, size_t);
/* Fixed MMU flush, event4 and END bytes. No execution proof. */
int sun60i_a733_npu_fe_run_image(void *, size_t);
#endif
