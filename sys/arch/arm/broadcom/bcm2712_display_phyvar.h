/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _ARM_BROADCOM_BCM2712_DISPLAY_PHYVAR_H_
#define _ARM_BROADCOM_BCM2712_DISPLAY_PHYVAR_H_

#include <sys/types.h>

/* Private fixed windows. The future native owner validates resources18/19. */
enum bcm2712_phy_window {
	BCM2712_PHY_WINDOW,
	BCM2712_RM_WINDOW
};
#define BCM2712_PHY_LENGTH 0x300U
#define BCM2712_RM_LENGTH 0x80U

/*
 * Callbacks finish before return. Read/write return positive BSD errno.
 * Barrier is mandatory and orders both windows after a successful write.
 * It cannot report or recover a stalled MMIO transaction or a bus abort.
 * The caller serializes calls and keeps callback storage and argument alive.
 * Power, geometry, mappings, clock leases and controller quiesce are outside
 * this pure mechanism. This interface does not authorize native MMIO.
 */
struct bcm2712_phy_io {
	int (*read)(void *, enum bcm2712_phy_window, unsigned int, uint32_t *);
	int (*write)(void *, enum bcm2712_phy_window, unsigned int, uint32_t);
	void (*barrier)(void *);
	void *arg;
};

enum bcm2712_phy_stage {
	BCM2712_PHY_IDLE,
	BCM2712_PHY_PROGRAM,
	BCM2712_PHY_PROGRAMMED,
	BCM2712_PHY_DISABLE,
	BCM2712_PHY_DISABLED
};
struct bcm2712_phy_status {
	enum bcm2712_phy_stage stage;
	unsigned int step; /* Current operation's one-based attempted step. */
	unsigned int reads, writes; /* Completed operations, cumulative. */
	int fault;
	bool mutation_attempted;
	bool programming_complete; /* True only while PROGRAMMED. */
	bool disable_complete; /* Sequence complete, not a power/release proof. */
};
struct bcm2712_phy {
	struct bcm2712_phy_io io;
	struct bcm2712_phy_status status;
	bool initialized;
};

/*
 * Init requires fresh zero-initialized storage. Never pass uninitialized
 * storage. Repeated init returns EBUSY, even after disable or fault.
 * Validation-only init/status leave output unchanged on error and do no I/O.
 */
int bcm2712_phy_init(struct bcm2712_phy *, const struct bcm2712_phy_io *);
/* Fixed HDMI0 RGB8 148.5 MHz TMDS profile. No timing or sink validation. */
int bcm2712_phy_program_148500(struct bcm2712_phy *);
/* Only after successful programming. No further I/O is allowed after fault. */
int bcm2712_phy_disable(struct bcm2712_phy *);
int bcm2712_phy_status(const struct bcm2712_phy *, struct bcm2712_phy_status *);
/* No fini, free, ownership release, PLL-lock or physical-link claim exists. */
#endif
