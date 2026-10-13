/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _SUN60I_A733_GPU_FWCOMPUTE_CMD_H
#define _SUN60I_A733_GPU_FWCOMPUTE_CMD_H

#include "powervr/pvr_rogue_fwif_client.h"

#define A733_GPU_FWCOMPUTE_BVNC UINT64_C(0x00240038006800b7)
#define A733_GPU_FWCOMPUTE_STREAM_BYTES 44U
#define A733_GPU_FWCOMPUTE_COMMAND_BYTES 48U

/*
 * Decode the pinned Mesa compute stream, then its u32 DRM submit flags.
 * The caller supplies validated resource bases. No memory mapping occurs.
 * EINVAL means malformed input. EOPNOTSUPP means an unsupported profile.
 * Leave the output unchanged on error.
 */
int sun60i_a733_gpu_fwcompute_cmd_decode(const void *, size_t, uint64_t,
    uint64_t, uint64_t, uint64_t, struct rogue_fwif_cmd_compute *);

#endif
