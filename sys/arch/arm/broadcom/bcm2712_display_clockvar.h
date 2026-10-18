/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _ARM_BROADCOM_BCM2712_DISPLAY_CLOCKVAR_H_
#define _ARM_BROADCOM_BCM2712_DISPLAY_CLOCKVAR_H_

#include <sys/types.h>

#define BCM2712_CLOCK_PORTS 2

/* Private source foundation. Only these three fixed firmware clock profiles exist. */
enum bcm2712_clock_profile {
	BCM2712_CLOCK_HSM,
	BCM2712_CLOCK_BVB,
	BCM2712_CLOCK_DISP
};

typedef int (*bcm2712_clock_exchange_fn)(void *, uint32_t *, size_t);

struct bcm2712_clock_status {
	uint32_t minimum[BCM2712_CLOCK_PORTS];
	uint32_t floor;
	uint32_t rate;
	uint32_t state;
	unsigned int leases;
	int fault;
	bool mutation_attempted;
};

/* The caller serializes operations and owns the exchange callback lifetime. */
struct bcm2712_clock {
	bcm2712_clock_exchange_fn exchange;
	void *arg;
	struct bcm2712_clock_status status;
	enum bcm2712_clock_profile profile;
	uint32_t initial_state;
	bool initialized;
	bool rate_changed;
	bool state_changed;
};

int bcm2712_clock_init(struct bcm2712_clock *, enum bcm2712_clock_profile,
    bcm2712_clock_exchange_fn, void *);
int bcm2712_clock_acquire(struct bcm2712_clock *, unsigned int, uint32_t);
int bcm2712_clock_update(struct bcm2712_clock *, unsigned int, uint32_t);
int bcm2712_clock_release(struct bcm2712_clock *, unsigned int);
int bcm2712_clock_status(const struct bcm2712_clock *,
    struct bcm2712_clock_status *);
int bcm2712_clock_fini(struct bcm2712_clock *);
#endif
