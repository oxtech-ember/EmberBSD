/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _ARM_BROADCOM_BCM2712_DISPLAY_BVB_H_
#define _ARM_BROADCOM_BCM2712_DISPLAY_BVB_H_

#include <sys/types.h>
#include <sys/device.h>
#include <dev/acpi/acpivar.h>

#define BCM2712_BVB_PORTS 2

struct bcm2712_bvb_provider;
struct bcm2712_bvb_status {
	uint32_t minimum[BCM2712_BVB_PORTS];
	uint32_t floor;
	uint32_t rate;
	uint32_t state;
	unsigned int leases;
	int fault;
	bool mutation_attempted;
};

/*
 * Process context only. Own the display handoff and exclude firmware and
 * unregistered clock writers before create. ACPI validation and ownership
 * of this native slot do not establish power or exclude other firmware users.
 * Create reads clock14 state/rate/max/min. It requires a positive minimum
 * and sends no SET. Acquire enables lazily; release keeps the firmware floor.
 * This provider never sends SET_CLOCKSTATE(0), including destroy.
 * Quiesce dependent hardware before release and API callers before destroy.
 * A fault retains this slot and its kernel-resident callback. Do not retry.
 * Create and status leave their output unchanged on error.
 */
int bcm2712_display_bvb_create(device_t, ACPI_HANDLE,
    struct bcm2712_bvb_provider **);
int bcm2712_display_bvb_acquire(struct bcm2712_bvb_provider *, unsigned int,
    uint32_t);
int bcm2712_display_bvb_update(struct bcm2712_bvb_provider *, unsigned int,
    uint32_t);
int bcm2712_display_bvb_release(struct bcm2712_bvb_provider *, unsigned int);
/* Cached observations only. No firmware request is sent, even after a fault. */
int bcm2712_display_bvb_status(struct bcm2712_bvb_provider *,
    struct bcm2712_bvb_status *);
int bcm2712_display_bvb_destroy(struct bcm2712_bvb_provider *);

#endif /* _ARM_BROADCOM_BCM2712_DISPLAY_BVB_H_ */
