/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD; common PowerVR compute stream profile decoder. */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _PVR_FWCOMPUTE_CMD_H
#define _PVR_FWCOMPUTE_CMD_H

#include "fwif/pvr_rogue_fwif_client.h"

#define PVR_FWCOMPUTE_A733_BVNC UINT64_C(0x00240038006800b7)
#define PVR_FWCOMPUTE_GE8300_BVNC UINT64_C(0x0016006600360026)
#define PVR_FWCOMPUTE_A733_STREAM_BYTES 44U
#define PVR_FWCOMPUTE_GE8300_STREAM_BYTES 36U
#define PVR_FWCOMPUTE_COMMAND_BYTES 48U

/*
 * Decode the pinned Mesa compute stream, then its u32 DRM submit flags.
 * The caller supplies validated resource bases. No memory mapping occurs.
 * EINVAL means malformed input. EOPNOTSUPP means an unsupported profile.
 * Leave the output unchanged on error.
 */
int pvr_fwcompute_cmd_decode(const void *, size_t, uint64_t,
    uint64_t, uint64_t, uint64_t, struct rogue_fwif_cmd_compute *);

#endif
