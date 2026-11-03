/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD; strict A733 adapter for common compute decoder. */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _SUN60I_A733_GPU_FWCOMPUTE_CMD_H
#define _SUN60I_A733_GPU_FWCOMPUTE_CMD_H
#include "../../../dev/powervr/pvr_fwcompute_cmd.h"
#define A733_GPU_FWCOMPUTE_BVNC PVR_FWCOMPUTE_A733_BVNC
#define A733_GPU_FWCOMPUTE_STREAM_BYTES PVR_FWCOMPUTE_A733_STREAM_BYTES
#define A733_GPU_FWCOMPUTE_COMMAND_BYTES PVR_FWCOMPUTE_COMMAND_BYTES
int sun60i_a733_gpu_fwcompute_cmd_decode(const void *, size_t, uint64_t,
    uint64_t, uint64_t, uint64_t, struct rogue_fwif_cmd_compute *);
#endif
