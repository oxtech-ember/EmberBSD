/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* MIT adaptation provenance and permission notice: pvr_stream.c. */
#ifndef PVR_NATIVE_STREAM_H
#define PVR_NATIVE_STREAM_H

#include <sys/types.h>
#ifndef _KERNEL
#include <stddef.h>
#include <stdint.h>
#endif

#define PVR_STREAM_BVNC UINT64_C(0x00240038006800b7)
#define PVR_STREAM_COMPUTE_SIZE 44U
#define PVR_STREAM_STATIC_COMPUTE_SIZE 64U
#define PVR_STREAM_STATIC_RENDER_SIZE 192U

struct rogue_fwif_cmd_compute;
struct rogue_fwif_cdm_registers_cswitch;
struct rogue_fwif_static_rendercontext_state;

/*
 * Decode already copied kernel buffers for the accepted A733 BVNC, MC1 profile.
 * Return positive errno. A rejected input leaves the entire output unchanged.
 * No allocation, copyin, firmware publication, or GPU address ownership occurs.
 * The caller must hold the VM, mapped BOs, and runtime context until retirement.
 * Common command attributes remain zero for the owning job layer to populate.
 * Unknown extensions and multicore execution are not supported by this slice.
 */
int pvr_stream_compute(uint64_t, const void *, size_t, uint32_t,
    struct rogue_fwif_cmd_compute *);
int pvr_stream_static_compute(uint64_t, const void *, size_t,
    struct rogue_fwif_cdm_registers_cswitch *);
int pvr_stream_static_render(uint64_t, const void *, size_t,
    struct rogue_fwif_static_rendercontext_state *);
int pvr_stream_static_transfer(uint64_t, const void *, size_t);

#endif /* PVR_NATIVE_STREAM_H */
