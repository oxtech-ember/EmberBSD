/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _ARM_BROADCOM_BCM2712_IOMMUC_H_
#define _ARM_BROADCOM_BCM2712_IOMMUC_H_

#include <sys/types.h>
#include <dev/acpi/acpivar.h>

#define BCM2712_IOMMUC_PATH "\\_SB.ICAC"
#define BCM2712_IOMMUC_PROPERTY "emberbsd,iommuc-contract-version"
#define BCM2712_IOMMUC_VERSION 1
#define BCM2712_IOMMUC_ADDRESS 0x1000005b00ULL
#define BCM2712_IOMMUC_LENGTH 0x80ULL
#define BCM2712_IOMMUC_MAX_DEPENDENCIES 32

struct bcm2712_iommuc_contract {
	uint32_t version;
	uint64_t address;
	uint64_t length;
};

/*
 * Process context only. These queries validate descriptions, not access rights.
 * No mapping, provider, clock, power operation or MMIO occurs. Outputs remain
 * unchanged on error. The dependency outputs use valid, disjoint storage.
 * Version 1 accepts at most 32 distinct Device references, including ICAC.
 */
int bcm2712_iommuc_acpi_resources(ACPI_HANDLE,
    struct bcm2712_iommuc_contract *);
int bcm2712_iommuc_acpi_dependency(ACPI_HANDLE, ACPI_HANDLE *,
    struct bcm2712_iommuc_contract *);

#endif
