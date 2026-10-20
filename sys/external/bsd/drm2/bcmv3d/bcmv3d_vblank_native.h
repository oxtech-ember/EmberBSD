/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _BCMV3D_VBLANK_NATIVE_H_
#define _BCMV3D_VBLANK_NATIVE_H_
#include <sys/bus.h>
#include <arm/broadcom/bcm2712_display.h>
struct bcmv3d_device;
struct bcmv3d_vblank_native;
struct bcmv3d_vblank_snapshot {
	uint32_t version, length;
	uint64_t sequence, spurious;
	uint32_t prepared, arm_attempted, routed, first_error;
	uint32_t channel_status, active_list, frame_count, reserved;
	uint32_t last_window, last_offset, last_raw, raw_valid;
	uint32_t reject_mask, reject_expected;
};
typedef void (*bcmv3d_vblank_notify_fn)(void *,
    const struct bcmv3d_vblank_snapshot *);
/*
 * Retained actual native owner only. The caller must first complete serial CPU
 * close, firmware ACK, both pipeline stops, MMU4 programming and initial HVS.
 * It holds the real four clocks and exclusive display ownership until reboot.
 * Supply the canonical contract and its actual retained PV0/HVS map handles.
 * Create takes the real PV0 GSI from that contract. It never borrows a boot
 * executing flag. All resources survive faults; there is no detach/reset API.
 * Process context without memory_lock. Outputs remain unchanged on failure.
 * Positive errno.
 */
int bcmv3d_vblank_native_create(struct bcmv3d_device *,
    const struct bcm2712_display_contract *, bus_space_handle_t,
    bus_space_handle_t, uint32_t, struct bcmv3d_vblank_native **);
int bcmv3d_vblank_native_snapshot(struct bcmv3d_vblank_native *,
    struct bcmv3d_vblank_snapshot *);
/*
 * Bind one retained display consumer. Retain callback text and argument
 * until reboot. The callback runs in interrupt context outside the native
 * mutex. It must not sleep. Copy the snapshot before returning if needed.
 * A nonzero first_error reports a route fault, not a frame event.
 * Prior events are not replayed as new events.
 */
/* Copy the baseline and bind under one IRQ mutex. No event is replayed. */
int bcmv3d_vblank_native_bind_snapshot(struct bcmv3d_vblank_native *,
    bcmv3d_vblank_notify_fn, void *, struct bcmv3d_vblank_snapshot *);
int bcmv3d_vblank_native_bind(struct bcmv3d_vblank_native *,
    bcmv3d_vblank_notify_fn, void *);
#endif
