/* SPDX-License-Identifier: MIT */
/* Origin: EmberBSD common PowerVR MIPS start, AI-assisted. */
/* Copyright (c) 2023 Imagination Technologies Ltd. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* See powervr/LICENSE for the MIT notice and a733-gpu-fwstart.md. */
#ifndef _PVR_FWSTART_H
#define _PVR_FWSTART_H

#include <sys/types.h>
#include <sys/bus.h>
#include "pvr_fwinfo.h"

#define PVR_FWSTART_REG_SIZE 0x10000

struct pvr_fwstart_result {
	uint32_t last_register, writes;
	bool touched, released;
};

/*
 * Explicit MIPS bootstrap for the closed A733 or experimental GE8300 profile. The caller must own
 * powered hardware and the complete mapped register bank. info must contain
 * metadata validated by the owner against its selected profile for the identified live GPU and remain stable. All firmware
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
int pvr_fwstart(enum pvr_fw_profile, bus_space_tag_t, bus_space_handle_t,
    const struct pvr_fwinfo *, uint64_t, uint64_t, uint64_t,
    struct pvr_fwstart_result *);

#endif
