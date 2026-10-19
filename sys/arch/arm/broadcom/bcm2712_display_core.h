/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _ARM_BROADCOM_BCM2712_DISPLAY_CORE_H_
#define _ARM_BROADCOM_BCM2712_DISPLAY_CORE_H_

#include <sys/types.h>
#include <sys/device.h>
#include <dev/acpi/acpivar.h>

#define BCM2712_CORE_SLOTS 2

struct bcm2712_core_provider;
struct bcm2712_core_status {
	uint32_t minimum[BCM2712_CORE_SLOTS];
	uint32_t floor;
	uint32_t rate;
	uint32_t state;
	unsigned int leases;
	int fault;
	bool mutation_attempted;
};

/*
 * Process context only. The caller owns global CORE clock policy and excludes
 * firmware and unregistered clock writers. CORE also serves CSI/bus consumers;
 * this slot is not a global consumer arbiter, power or display-access proof.
 * Create GETs clock4 state/rate/max/min and sends no SET. MIN and inherited RATE
 * must be positive; floor=max(MIN,inherited RATE) must not exceed MAX. The floor
 * is frozen for this owner lifetime, even if the clock was initially off.
 * Logical slots0/1 are not HDMI ports. Acquisition activates lazily. No release
 * or destroy lowers below floor or sends SET_CLOCKSTATE(0). Stop all dependent
 * consumers before release; quiesce API callers before destroy. A failed SET
 * retains this fixed slot and kernel callback. No retry or replacement follows.
 * Create/status preserve output on error; status is cached, not a fresh sample.
 */
int bcm2712_display_core_create(device_t, ACPI_HANDLE,
    struct bcm2712_core_provider **);
int bcm2712_display_core_acquire(struct bcm2712_core_provider *, unsigned int,
    uint32_t);
int bcm2712_display_core_update(struct bcm2712_core_provider *, unsigned int,
    uint32_t);
int bcm2712_display_core_release(struct bcm2712_core_provider *, unsigned int);
/* Cached observations only. No firmware request is sent, even after a fault. */
int bcm2712_display_core_status(struct bcm2712_core_provider *,
    struct bcm2712_core_status *);
int bcm2712_display_core_destroy(struct bcm2712_core_provider *);

#endif /* _ARM_BROADCOM_BCM2712_DISPLAY_CORE_H_ */
