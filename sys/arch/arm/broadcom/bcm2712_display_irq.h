/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _ARM_BROADCOM_BCM2712_DISPLAY_IRQ_H_
#define _ARM_BROADCOM_BCM2712_DISPLAY_IRQ_H_

#include <sys/types.h>
#include <arm/broadcom/bcm2712_display.h>

enum bcm2712_l2_domain {
	BCM2712_L2_DISP,
	BCM2712_L2_AON,
	BCM2712_L2_BSC
};

/* The caller serializes every core operation at the parent interrupt IPL. */
struct bcm2712_l2_io {
	uint32_t (*read)(void *, unsigned int);
	void (*write)(void *, unsigned int, uint32_t);
	void (*barrier)(void *);
	void *arg;
};

struct bcm2712_l2 {
	struct bcm2712_l2_io io;
	uint32_t owned, declared, pending, saved_mask;
	unsigned int mask_status, mask_set, mask_clear, clear;
	bool has_clear, prepared, armed;
	int fault;
};

int bcm2712_l2_init(struct bcm2712_l2 *,
    const struct bcm2712_display_contract *, enum bcm2712_l2_domain,
    const struct bcm2712_display_memory *, uint32_t,
    const struct bcm2712_l2_io *);
int bcm2712_l2_prepare(struct bcm2712_l2 *);
int bcm2712_l2_arm(struct bcm2712_l2 *);
int bcm2712_l2_collect(struct bcm2712_l2 *, uint32_t *);
uint32_t bcm2712_l2_take(struct bcm2712_l2 *);
void bcm2712_l2_complete(struct bcm2712_l2 *, uint32_t, int);
void bcm2712_l2_stop(struct bcm2712_l2 *, int);
void bcm2712_l2_rollback(struct bcm2712_l2 *);

#ifdef _KERNEL
#include <sys/bus.h>
#include <sys/device.h>

struct bcm2712_l2_provider;
/* Process context. A nonzero error means the controller route is stopped. */
typedef int (*bcm2712_display_irq_fn)(void *, uint32_t, int);

/*
 * The lifecycle owner must hold power, clocks and the one mapped resource.
 * A GET-clock snapshot is insufficient. This API does not map or own clocks.
 * Keep the mapping and callback argument alive until destroy returns zero.
 * Do not register overlapping providers for one domain or parent GSI.
 */
int bcm2712_display_irq_create(device_t,
    const struct bcm2712_display_contract *, enum bcm2712_l2_domain,
    const struct bcm2712_display_memory *, bus_space_tag_t, bus_space_handle_t,
    uint32_t, bcm2712_display_irq_fn, void *, struct bcm2712_l2_provider **);
/* Sleepable lifecycle context only; callback self-destruction returns EDEADLK. */
int bcm2712_display_irq_destroy(struct bcm2712_l2_provider *);
int bcm2712_display_irq_error(struct bcm2712_l2_provider *);
#endif
#endif
