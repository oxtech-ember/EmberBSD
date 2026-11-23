/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _ARM_ACPI_CIX_UART2_CONSOLE_H_
#define _ARM_ACPI_CIX_UART2_CONSOLE_H_

/*
 * Early thread context. Zero identifies a known CIX P1 DSDT, which has the
 * DEBUG UART2 at a fixed address. Any other value means "not identified":
 * the caller then uses its ordinary console selection. The probe only reads
 * firmware tables; it does no device access. Native firmware mapping and
 * immutability remain trusted; this is not security validation of all ACPI
 * data. Existing PSCI/MADT discovery is unchanged.
 */
int cix_uart2_console_probe(void);

#endif
