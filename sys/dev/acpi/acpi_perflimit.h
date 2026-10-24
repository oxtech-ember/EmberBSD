/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: original ACPI processor performance limit broker, AI-assisted. */

#ifndef _DEV_ACPI_ACPI_PERFLIMIT_H_
#define _DEV_ACPI_ACPI_PERFLIMIT_H_

#include <sys/types.h>

struct cpu_info;

/* A limit is a percentage of the provider's performance range. */
#define ACPI_PERFLIMIT_NONE	100u
#define ACPI_PERFLIMIT_OWNERS	8

/*
 * Thread context only. A provider (for example CPPC) owns one CPU and
 * applies the strictest limit that its owners request. Each owner, such
 * as a thermal zone, keeps at most one request per CPU; ACPI_PERFLIMIT_NONE
 * withdraws it. apply is called with the broker lock held and must not
 * sleep; a failed apply keeps the request and the previous applied limit.
 *
 * set returns ENODEV without a provider, ENOSPC without a free owner slot
 * and the provider's error when applying fails.
 */
int	acpi_perflimit_attach(struct cpu_info *, int (*)(void *, u_int),
	    void *);
void	acpi_perflimit_detach(struct cpu_info *);
int	acpi_perflimit_set(struct cpu_info *, const void *, u_int);
u_int	acpi_perflimit_applied(struct cpu_info *);

#endif /* _DEV_ACPI_ACPI_PERFLIMIT_H_ */
