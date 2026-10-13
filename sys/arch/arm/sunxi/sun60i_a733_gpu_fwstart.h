/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* See powervr/LICENSE for the MIT notice and a733-gpu-fwstart.md. */
#ifndef _SUN60I_A733_GPU_FWSTART_H
#define _SUN60I_A733_GPU_FWSTART_H

#include <sys/types.h>
#include <sys/bus.h>
#include <arm/sunxi/sun60i_a733_gpu_fwinfo.h>

#define A733_GPU_FWSTART_REG_SIZE 0x10000

struct sun60i_a733_gpu_fwstart_result {
	uint32_t last_register, writes;
	bool touched, released;
};

/*
 * Explicit MIPS bootstrap for BVNC 36.56.104.183 only. The caller must own
 * powered hardware and the complete mapped register bank. info must contain
 * validated metadata for the identified live GPU and remain stable. All firmware
 * allocations, page tables and FWIF structures must already be initialized,
 * synchronized for device access and retained until a proven hardware reset.
 * No allocation is freed here. A failed write can have side effects: touched
 * becomes true BEFORE the first write, including a faulting write.
 * Return positive errno. A non-NULL result is cleared before validation.
 * last_register is the last attempted access; writes counts attempted writes.
 * released means the reset-release write completed, not that firmware booted.
 * A faulting reset-release write can start the GPU while released stays false.
 * Any touched result requires retained memory, even when released is false.
 */
int sun60i_a733_gpu_fwstart(bus_space_tag_t, bus_space_handle_t,
    const struct sun60i_a733_gpu_fwinfo *, uint64_t, uint64_t, uint64_t,
    struct sun60i_a733_gpu_fwstart_result *);

#endif
