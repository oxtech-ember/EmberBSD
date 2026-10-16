/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _ARM_BROADCOM_BCM2712_DISPLAY_HSM_H_
#define _ARM_BROADCOM_BCM2712_DISPLAY_HSM_H_

#include <sys/types.h>

#define BCM2712_HSM_PORTS 2

/* Byte length includes the property header and end tag. No MMIO transport. */
typedef int (*bcm2712_hsm_exchange_fn)(void *, uint32_t *, size_t);

struct bcm2712_hsm_status {
	uint32_t minimum[BCM2712_HSM_PORTS];
	uint32_t rate;
	uint32_t state;
	unsigned int leases;
	int fault;
	bool mutation_attempted;
};

/* Serialized by the native provider. Zero rate is valid only before a lease. */
struct bcm2712_hsm {
	bcm2712_hsm_exchange_fn exchange;
	void *arg;
	struct bcm2712_hsm_status status;
	uint32_t floor;
	uint32_t initial_state;
	bool initialized;
	bool rate_changed;
	bool state_changed;
};

int bcm2712_hsm_init(struct bcm2712_hsm *, bcm2712_hsm_exchange_fn, void *);
int bcm2712_hsm_acquire(struct bcm2712_hsm *, unsigned int, uint32_t);
int bcm2712_hsm_update(struct bcm2712_hsm *, unsigned int, uint32_t);
int bcm2712_hsm_release(struct bcm2712_hsm *, unsigned int);
int bcm2712_hsm_fini(struct bcm2712_hsm *);
#endif
