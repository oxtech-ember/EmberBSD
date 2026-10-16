/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _ARM_BROADCOM_BCM2712_DISPLAY_ACPI_H_
#define _ARM_BROADCOM_BCM2712_DISPLAY_ACPI_H_
#include <sys/device.h>
#include <dev/acpi/acpivar.h>
#include <arm/broadcom/bcm2712_display.h>

/* Resource validation does not require an enabled display clock. */
int bcm2712_display_acpi_resources(device_t, ACPI_HANDLE,
    struct bcm2712_display_contract *);

int bcm2712_display_acpi_query(device_t, ACPI_HANDLE,
    struct bcm2712_display_contract *, struct bcm2712_display_clocks *);
void bcm2712_display_acpi_observe(device_t, ACPI_HANDLE);

struct bcm2712_hsm_provider;
struct bcm2712_hsm_status;
/* Explicit GPU0 lifecycle; no automatic attachment or display MMIO. */
int bcm2712_display_hsm_create(device_t, ACPI_HANDLE,
    struct bcm2712_hsm_provider **);
int bcm2712_display_hsm_acquire(struct bcm2712_hsm_provider *, unsigned int,
    uint32_t);
int bcm2712_display_hsm_update(struct bcm2712_hsm_provider *, unsigned int,
    uint32_t);
int bcm2712_display_hsm_release(struct bcm2712_hsm_provider *, unsigned int);
int bcm2712_display_hsm_status(struct bcm2712_hsm_provider *,
    struct bcm2712_hsm_status *);
int bcm2712_display_hsm_destroy(struct bcm2712_hsm_provider *);
#endif
