/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _ARM_ACPI_CIX_UART2_CONSOLE_H_
#define _ARM_ACPI_CIX_UART2_CONSOLE_H_

/*
 * Early thread context. Zero qualifies only the fixed historical UART2 profile.
 * EBUSY reports SPCR present after the bounded console-header preflight.
 * Native firmware mapping/immutability remains trusted; this is not security
 * validation of all ACPI data. Existing PSCI/MADT discovery is unchanged.
 */
int cix_uart2_console_probe(void);

#endif
