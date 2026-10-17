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
 * ID 0x43 supports gate control and rate queries. IDs 0x108 and 0x10a support
 * rate queries only; enable/disable returns ENOTSUP without evaluating AML.
 * This matrix limits the driver API. Firmware can reject a supported request.
 * Other IDs are unsupported. Claims are exclusive per ID. Different IDs have
 * independent claims. A claim does not change hardware.
 * Outputs are unchanged on failure. No operation changes the clock rate.
 * Repeated confirmed gate enable/disable returns EALREADY without evaluating AML.
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
