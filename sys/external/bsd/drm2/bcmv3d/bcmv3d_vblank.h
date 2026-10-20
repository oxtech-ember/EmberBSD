/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _BCMV3D_VBLANK_H_
#define _BCMV3D_VBLANK_H_
#include <sys/types.h>

enum bcmv3d_vblank_window { BCMV3D_VBLANK_PV0, BCMV3D_VBLANK_HVS };
struct bcmv3d_vblank_io {
	int (*read)(void *, enum bcmv3d_vblank_window, uint32_t, uint32_t *);
	int (*write)(void *, enum bcmv3d_vblank_window, uint32_t, uint32_t);
	void (*barrier)(void *, enum bcmv3d_vblank_window);
};
struct bcmv3d_vblank {
	const struct bcmv3d_vblank *owner;
	const struct bcmv3d_vblank_io *io;
	void *arg;
	uint64_t sequence, spurious;
	uint32_t variant, control_offset, status_offset, active_offset;
	uint32_t channel_status, active_list, frame_count;
	uint32_t last_window, last_offset, last_raw, reject_mask, reject_expected;
	bool raw_valid;
	int fault;
	bool attempted, prepared, armed;
};
/*
 * Private zeroed stable storage. The native owner serializes all calls at its
 * IRQ IPL. It retains clocks, canonical PV0/HVS mappings and callback storage.
 * Prepare requires the actual initial HVS head32/1080p60 and exclusive PV0
 * ownership. It verifies the running channel, masks inherited PV0 sources and checks
 * INTEN0 before the first
 * W1C. Call arm only after a real IRQ has been established. Native handling
 * must tolerate a spurious interrupt before arm. No release/reset API exists.
 * Failures are sticky. After I/O failure, mask the real parent IRQ without
 * further display MMIO. Keep all hardware resources alive until reboot.
 */
int bcmv3d_vblank_prepare(struct bcmv3d_vblank *,
    const struct bcmv3d_vblank_io *, void *, uint32_t);
int bcmv3d_vblank_arm(struct bcmv3d_vblank *);
/*
 * handled is false on error or a spurious source. Only an observed VFP_START,
 * successful W1C and actual running-HVS sample advance sequence. The frame
 * count is six-bit hardware data. It is not inferred from elapsed time.
 * These events alone do not establish BO retirement or DRM page-flip delivery.
 */
int bcmv3d_vblank_irq(struct bcmv3d_vblank *, bool *);
#endif
