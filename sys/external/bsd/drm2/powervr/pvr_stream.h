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
#define PVR_STREAM_GEOMETRY_SIZE 40U
#define PVR_STREAM_FRAGMENT_SIZE 384U
#define PVR_STREAM_TRANSFER_SIZE 176U
#define PVR_STREAM_STATIC_COMPUTE_SIZE 64U
#define PVR_STREAM_STATIC_RENDER_SIZE 192U

struct rogue_fwif_cmd_compute;
struct rogue_fwif_cmd_geom;
struct rogue_fwif_cmd_frag;
struct rogue_fwif_cmd_transfer;
struct rogue_fwif_cdm_registers_cswitch;
struct rogue_fwif_static_rendercontext_state;

/*
 * Decode already copied kernel buffers for the accepted A733 BVNC, MC1 profile.
 * Return positive errno. A rejected input leaves the entire output unchanged.
 * No allocation, copyin, firmware publication, or GPU address ownership occurs.
 * The caller must hold the VM, mapped BOs, and runtime context until retirement.
 * Common attributes, HWRT references and the partial-render fence remain zero.
 * The job owner must populate them and retain both paired geometry/PR queues.
 * Unknown extensions and multicore execution are not supported by this slice.
 */
int pvr_stream_compute(uint64_t, const void *, size_t, uint32_t,
    struct rogue_fwif_cmd_compute *);
int pvr_stream_geometry(uint64_t, const void *, size_t, uint32_t,
    struct rogue_fwif_cmd_geom *);
/* command_type selects FRAG or FRAG_PR. Both distinct outputs change only on success. */
int pvr_stream_fragment(uint64_t, const void *, size_t, uint32_t,
    struct rogue_fwif_cmd_frag *, uint32_t *command_type);
int pvr_stream_transfer(uint64_t, const void *, size_t, uint32_t,
    struct rogue_fwif_cmd_transfer *);
int pvr_stream_static_compute(uint64_t, const void *, size_t,
    struct rogue_fwif_cdm_registers_cswitch *);
int pvr_stream_static_render(uint64_t, const void *, size_t,
    struct rogue_fwif_static_rendercontext_state *);
int pvr_stream_static_transfer(uint64_t, const void *, size_t);

#endif /* PVR_NATIVE_STREAM_H */
