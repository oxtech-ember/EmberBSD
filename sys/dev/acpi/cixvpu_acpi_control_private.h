/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Private coupling for the two bounded control consumers, AI-assisted. */
#ifndef _DEV_ACPI_CIXVPU_ACPI_CONTROL_PRIVATE_H_
#define _DEV_ACPI_CIXVPU_ACPI_CONTROL_PRIVATE_H_

#include <dev/acpi/cixvpu_acpi_control.h>

int cixvpu_control_claim_native(struct cixvpu_acpi_control *, device_t,
    const struct acpi_attach_args *, const struct cixvpu_resources *);
int cixvpu_control_activate(struct cixvpu_acpi_control *,
    const struct cixvpu_resources *);
int cixvpu_control_boot_profile(struct cixvpu_acpi_control *,
    const struct cixvpu_resources *);
int cixvpu_control_assert_stop(struct cixvpu_acpi_control *);
int cixvpu_control_clock_stop(struct cixvpu_acpi_control *);
int cixvpu_control_core_resources(struct cixvpu_acpi_control *,
    ACPI_HANDLE[4], ACPI_HANDLE[4]);
#endif
