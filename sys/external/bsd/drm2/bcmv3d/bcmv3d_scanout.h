/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _BCMV3D_SCANOUT_H_
#define _BCMV3D_SCANOUT_H_
#include <sys/bus.h>
#include <arm/broadcom/bcm2712_display.h>
struct bcmv3d_device;
struct bcmv3d_mmu4_native;
struct bcmv3d_scanout;
struct bcmv3d_scanout_frame;
struct drm_gem_object;

struct bcmv3d_scanout_status {
	uint32_t version, length;
	uint64_t sequence, frame_epoch, latches, prepared, retired;
	int32_t current_slot, pending_slot;
	uint32_t state[4], first_error, worker_pending;
};
typedef void (*bcmv3d_scanout_notify_fn)(void *, uint64_t, bool, int);

/*
 * Retained native HVS owner. Create only after the canonical serial/firmware
 * close, four clock leases, dual-stop, MMU4, initial HVS and PV0 IRQ route.
 * The caller owns SRAM32..72 inclusive and UPM0..62463/slots0..3 exclusively.
 * It supplies its real retained canonical HVS map and observed CXM capacity.
 * No boot executing flag, disabled-bit or metadata grants this ownership.
 * All mappings, clocks, owner storage and callbacks survive until reboot.
 * No suspend/detach API. Process context, without memory_lock or modeset locks.
 */
int bcmv3d_scanout_create(struct bcmv3d_device *,
    const struct bcm2712_display_contract *, bus_space_handle_t, uint32_t,
    uint32_t, struct bcmv3d_scanout **);
/* Retain callback text and argument until reboot. IRQ context, no sleep. */
int bcmv3d_scanout_bind(struct bcmv3d_scanout *, bcmv3d_scanout_notify_fn, void *);

/*
 * Caller holds a live native GEM reference. Fixed1920x1080/XRGB8888/linear,
 * pitch7680/offset0. Do not call while holding an engine/native hardware lock
 * or a BO reservation lock. Reserve captures the original writer and publishes
 * one reader without waiting for rendering. It stages and publishes only
 * unexposed translations. It does not sync the consumer data or access HVS.
 * A matching RESERVED/READY/PENDING/CURRENT BO reuses its original lease before
 * reservation locking. Finalize waits for the retained writer outside native
 * owner locks, checks its status, then syncs the real consumer DMA map to READY.
 * Finalize allocates no storage. It takes a positive writer-wait tick budget.
 * A failed finalize leaves the reference owned by the caller. Put cancels an
 * unexposed RESERVED or READY slot. Uncertain cleanup retains resources.
 * Prepare combines reserve and finalize with the existing five-second budget.
 * Each successful reserve or prepare returns one software reference.
 */
int bcmv3d_scanout_reserve(struct bcmv3d_scanout *, struct drm_gem_object *,
    struct bcmv3d_scanout_frame **);
int bcmv3d_scanout_finalize(struct bcmv3d_scanout_frame *, long);
int bcmv3d_scanout_prepare(struct bcmv3d_scanout *, struct drm_gem_object *,
    struct bcmv3d_scanout_frame **);
/*
 * Wait for an old GEM consumer to retire and finish actual map/read cleanup.
 * The caller retains its frame reference throughout this call. This function
 * never consumes that reference. CURRENT and PENDING frames return EBUSY.
 * Wait outside native, engine and BO reservation locks. The positive tick
 * budget covers the reader-fence wait. Linux ERESTARTSYS becomes BSD EINTR.
 * Other errors outside the BSD errno range become EIO. Recheck under the lock
 * proves that the worker has completed reclaim after signalling the reader.
 * An already retired frame with a software reference succeeds without waiting.
 * This API does not cover the reserved empty-list slot. Positive BSD errno.
 */
int bcmv3d_scanout_wait_retired(struct bcmv3d_scanout_frame *, long);
int bcmv3d_scanout_get(struct bcmv3d_scanout_frame *);
int bcmv3d_scanout_put(struct bcmv3d_scanout_frame *);
bool bcmv3d_scanout_matches(struct bcmv3d_scanout_frame *,
    struct drm_gem_object *);
/*
 * NULL selects the reserved empty list with black background. A real later
 * active-list snapshot completes the flip. One pending publication at a time.
 * Error after exposure is sticky and retains resources; it is not a flip.
 */
int bcmv3d_scanout_submit(struct bcmv3d_scanout *,
    struct bcmv3d_scanout_frame *);
int bcmv3d_scanout_cached_status(struct bcmv3d_scanout *,
    struct bcmv3d_scanout_status *);

/*
 * Internal MMU4 authorization only. The actual process worker owns the exact
 * frame in RECLAIMING after real HVS retirement. A copied frame, wrong native
 * MMU/slot/BO or current/pending slot cannot authorize unmap. This is not a
 * caller-provided bool or a userspace interface. Positive BSD errno.
 */
int bcmv3d_scanout_retirement_check(struct bcmv3d_scanout_frame *,
    struct bcmv3d_mmu4_native *, unsigned int, struct drm_gem_object *);
#endif
