/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _ARM_BROADCOM_BCM2712_DISPLAY_ACPI_H_
#define _ARM_BROADCOM_BCM2712_DISPLAY_ACPI_H_
#include <sys/device.h>
#include <dev/acpi/acpivar.h>
#include <arm/broadcom/bcm2712_display.h>

int bcm2712_display_acpi_query(device_t, ACPI_HANDLE,
    struct bcm2712_display_contract *, struct bcm2712_display_clocks *);
void bcm2712_display_acpi_observe(device_t, ACPI_HANDLE);
#endif
