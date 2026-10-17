/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: original checked CIX ACPI clock interface, AI-assisted. */

#ifndef _DEV_ACPI_CIXCLK_ACPI_H_
#define _DEV_ACPI_CIXCLK_ACPI_H_

#include <sys/types.h>
#include <dev/acpi/acpireg.h>

struct cixclk;

/*
 * Sleepable, boot-long provider. No interrupt or hot-detach support.
 * Only clock 0x43 is supported. A claim is exclusive and does not change hardware.
 * Outputs are unchanged on failure. No operation changes the clock rate.
 * Repeated enable/disable returns EALREADY without evaluating AML.
 * Failed control retains the claim in an uncertain state. Disable can retry
 * rollback. Put returns EBUSY until disable succeeds. Enable returns EIO while
 * uncertain. The caller must not use the claim after a successful put.
 */
int cixclk_acpi_get(ACPI_HANDLE, uint32_t, struct cixclk **);
int cixclk_acpi_enable(struct cixclk *);
int cixclk_acpi_disable(struct cixclk *);
int cixclk_acpi_get_rate(struct cixclk *, uint64_t *);
int cixclk_acpi_put(struct cixclk *);

#endif /* _DEV_ACPI_CIXCLK_ACPI_H_ */
