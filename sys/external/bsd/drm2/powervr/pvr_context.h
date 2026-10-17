/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted native implementation. */
#ifndef _PVR_CONTEXT_H_
#define _PVR_CONTEXT_H_
#include <sys/types.h>
struct pvr_backend;
struct pvr_context;
struct rogue_fwif_cmd_compute;
struct rogue_fwif_cmd_geom;
struct rogue_fwif_cmd_frag;
struct rogue_fwif_cmd_transfer;
struct pvr_context_config {
	uint32_t type, priority, id, pid, vm_address;
	uint64_t callstack;
	const void *static_state;
	size_t static_size;
};
/*
 * Caller holds the device and backend gates, plus the owning VM reference.
 * Positive native errno. The output slot starts NULL. Any non-NULL result is
 * owned by the caller, including a partially created object returned on error.
 * Destroy clears the slot only after acknowledged cleanup and all DMA frees.
 * A failed destroy retains the object and requires the VM owner to survive.
 * Compute submission is synchronous. Success requires the real client UFO,
 * the consumed CCCB read offset and a fresh firmware health observation.
 */
int pvr_context_create(const struct pvr_backend *, const struct pvr_context_config *,
    struct pvr_context **);
int pvr_context_destroy(struct pvr_context **);
int pvr_context_compute(struct pvr_context *, const struct rogue_fwif_cmd_compute *, uint32_t);
/* The caller also retains the HWRT owner until completion or acknowledged cleanup.
 * A pair publishes both CCCBs in one combined kick with the geometry UFO.
 * Standalone partial-render jobs are unsupported. Do not wait for geometry
 * before submitting its paired fragment command.
 */
int pvr_context_render_pair(struct pvr_context *, const struct rogue_fwif_cmd_geom *,
    const struct rogue_fwif_cmd_frag *, uint32_t, uint32_t, uint32_t, uint32_t);
int pvr_context_fragment(struct pvr_context *, const struct rogue_fwif_cmd_frag *,
    uint32_t, uint32_t);
int pvr_context_transfer(struct pvr_context *, const struct rogue_fwif_cmd_transfer *,
    uint32_t);
uint32_t pvr_context_type(const struct pvr_context *);
#endif /* _PVR_CONTEXT_H_ */
