/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: original bounded native CIX reset API, AI-assisted. */

#ifndef _DEV_ACPI_CIXRESET_ACPI_H_
#define _DEV_ACPI_CIXRESET_ACPI_H_

#include <sys/types.h>
#include <sys/stdbool.h>
#include <dev/acpi/acpica.h>

struct cixreset;

/*
 * Thread context only. Claims are exclusive and perform no MMIO at get.
 * Only CIX P1 RST0 IDs 0x0e and 0x8e are supported. Errors leave *reset
 * unchanged. The provider is built in and has no detach operation.
 *
 * assert clears the active-low bit; deassert sets it. Each operation checks
 * readback. On error, keep the claim and recover with a confirmed assert.
 * put succeeds only for an untouched or confirmed asserted claim. A caller
 * must stop using a claim after a successful put and serialize its lifetime.
 * Timing and the device power/clock sequence belong to the consumer.
 */
int cixreset_acpi_get(ACPI_HANDLE, uint32_t, struct cixreset **);
int cixreset_acpi_assert(struct cixreset *);
int cixreset_acpi_deassert(struct cixreset *);
int cixreset_acpi_put(struct cixreset *);
/*
 * Read one owned reset bit, without writes or changes to claim state.
 * Errors leave *asserted unchanged. A read cannot recover an uncertain claim.
 */
int cixreset_acpi_get_state(struct cixreset *, bool *asserted);

#endif /* _DEV_ACPI_CIXRESET_ACPI_H_ */
