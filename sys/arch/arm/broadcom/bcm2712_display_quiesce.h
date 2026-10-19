/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _ARM_BROADCOM_BCM2712_DISPLAY_QUIESCE_H_
#define _ARM_BROADCOM_BCM2712_DISPLAY_QUIESCE_H_

#include <sys/types.h>
#ifndef _KERNEL
#include <stdbool.h>
#include <stdint.h>
#endif

#define BCM2712_QUIESCE_HVS 0
#define BCM2712_QUIESCE_PV0 1
#define BCM2712_QUIESCE_PV1 2
#define BCM2712_QUIESCE_REGIONS 3

/* Register access only. These operations confer no native admission right. */
struct bcm2712_quiesce_ops {
	int (*read)(void *, unsigned int, uint32_t, uint32_t *);
	int (*write)(void *, unsigned int, uint32_t, uint32_t);
	void (*barrier)(void *, unsigned int);
	void (*delay)(void *, unsigned int);
};

struct bcm2712_quiesce_snapshot {
	uint32_t global;
	uint32_t control[3], status[3];
	uint32_t pv_control[2], pv_video[2];
};

/* Zero-initialize once. A partial snapshot is invalid unless snapshot_valid. */
struct bcm2712_quiesce_status {
	struct bcm2712_quiesce_snapshot before;
	uint32_t version, pv_control, pv_video, control, status;
	bool attempted, snapshot_valid, mutation_attempted, complete;
	int error;
};

/*
 * The caller holds clocks and mappings, and excludes other display writers.
 * Stop only PV0/HVS0. Keep global HVS enable and all other pipelines unchanged.
 * RESET may remain set. EMPTY is an observation; this is not an AXI drain.
 * Every read/write error is terminal. No retry, rollback or release is done.
 */
int bcm2712_display_quiesce(const struct bcm2712_quiesce_ops *, void *,
    struct bcm2712_quiesce_status *);
#endif
