/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _ARM_BROADCOM_BCM2712_DISPLAY_DISP_H_
#define _ARM_BROADCOM_BCM2712_DISPLAY_DISP_H_
#include <sys/types.h>
#include <sys/device.h>
#include <dev/acpi/acpivar.h>

/* Logical lease slots for one global HVS clock, not physical HDMI ports. */
#define BCM2712_DISP_SLOTS 2
struct bcm2712_disp_provider;
struct bcm2712_disp_status {
	uint32_t minimum[BCM2712_DISP_SLOTS];
	uint32_t floor;
	uint32_t rate;
	uint32_t state;
	unsigned int leases;
	int fault;
	bool mutation_attempted;
};
/*
 * Process context only. The caller owns display handoff and excludes firmware
 * and unregistered clock writers. The fixed slot proves neither power nor MMIO
 * safety. Create GETs clock16 state/rate/max/min and requires a positive minimum.
 * Activation is lazy. No guessed HVS clock rate or SET_RATE0 is permitted.
 * Release requests the firmware floor; destroy restores off only if initially
 * off and enabled by this owner. Stop all dependent consumers before release.
 * Quiesce API callers before destroy. Never retry a retained SET fault.
 * The module that supplies these functions/callbacks must remain resident while
 * an owner exists, including after faults. Failed create/status preserve out.
 */
int bcm2712_display_disp_create(device_t, ACPI_HANDLE,
    struct bcm2712_disp_provider **);
int bcm2712_display_disp_acquire(struct bcm2712_disp_provider *, unsigned int,
    uint32_t);
int bcm2712_display_disp_update(struct bcm2712_disp_provider *, unsigned int,
    uint32_t);
int bcm2712_display_disp_release(struct bcm2712_disp_provider *, unsigned int);
/* Cached only, including after a fault. No firmware request is sent. */
int bcm2712_display_disp_status(struct bcm2712_disp_provider *,
    struct bcm2712_disp_status *);
int bcm2712_display_disp_destroy(struct bcm2712_disp_provider *);
#endif /* _ARM_BROADCOM_BCM2712_DISPLAY_DISP_H_ */
