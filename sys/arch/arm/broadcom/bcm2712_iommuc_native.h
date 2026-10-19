/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _ARM_BROADCOM_BCM2712_IOMMUC_NATIVE_H_
#define _ARM_BROADCOM_BCM2712_IOMMUC_NATIVE_H_

#include <dev/acpi/acpivar.h>
#include <arm/broadcom/bcm2712_iommuc_corevar.h>

struct bcm2712_iommuc_provider;

enum bcm2712_iommuc_observe_stage {
	BCM2712_IOMMUC_OBSERVE_NONE,
	BCM2712_IOMMUC_OBSERVE_MAP,
	BCM2712_IOMMUC_OBSERVE_READ,
	BCM2712_IOMMUC_OBSERVE_COMPLETE
};

struct bcm2712_iommuc_observation {
	enum bcm2712_iommuc_observe_stage stage;
	uint32_t raw_control;
	unsigned int reads_attempted, reads_completed;
	int first_error;
	bool started, mapped, raw_valid;
};

struct bcm2712_iommuc_native_status {

	bool software_attached;
	bool core_initialized;
	struct bcm2712_iommuc_status core;
	struct bcm2712_iommuc_observation observation;
};

/*
 * Process context only. The provider has built-in, boot-lifetime storage.
 * Lookup validates the canonical descriptor and completed ACPI attachment.
 * Cached status does not evaluate AML. Lookup/status do not map or access hardware.
 * Software attachment is not power, ownership, readiness or an access grant.
 * The core remains uninitialized until a separate access contract exists.
 * Outputs remain unchanged on error. Callers must not free the provider.
 */
int bcm2712_iommuc_lookup(ACPI_HANDLE, struct bcm2712_iommuc_provider **);
int bcm2712_iommuc_cached_status(struct bcm2712_iommuc_provider *,
    struct bcm2712_iommuc_native_status *);

/*
 * Explicit experimental one-shot CONTROL+0 observation. The caller establishes
 * normal-active SoC accessibility and excludes firmware and all MMU2/4/5/cache
 * writers. Those external conditions are not proved by this API or a sample.
 * Map/read execute under the provider mutex in process context. A successful
 * map has boot lifetime, including read rejection. There is no unmap or retry.
 * Cached observation preserves the first error and any returned raw value.
 * A returned sample does not initialize the core or authorize a cache write.
 * Native bus stalls/aborts are not recoverable or bounded by a software timer.
 */
int bcm2712_iommuc_observe_control(struct bcm2712_iommuc_provider *);

#endif
