/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _ARM_BROADCOM_BCM2712_IOMMUC_NATIVE_H_
#define _ARM_BROADCOM_BCM2712_IOMMUC_NATIVE_H_

#include <dev/acpi/acpivar.h>
#include <arm/broadcom/bcm2712_iommuc_corevar.h>

struct bcm2712_iommuc_provider;

struct bcm2712_iommuc_native_status {
	bool software_attached;
	bool core_initialized;
	struct bcm2712_iommuc_status core;
};

/*
 * Process context only. The provider has built-in, boot-lifetime storage.
 * Lookup validates the canonical descriptor and completed ACPI attachment.
 * Cached status does not evaluate AML. Neither API maps or accesses hardware.
 * Software attachment is not power, ownership, readiness or an access grant.
 * The core remains uninitialized until a separate access contract exists.
 * Outputs remain unchanged on error. Callers must not free the provider.
 */
int bcm2712_iommuc_lookup(ACPI_HANDLE, struct bcm2712_iommuc_provider **);
int bcm2712_iommuc_cached_status(struct bcm2712_iommuc_provider *,
    struct bcm2712_iommuc_native_status *);

#endif
