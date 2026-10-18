/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: original checked native CIX clock67 service, AI-assisted. */
#ifndef _DEV_ACPI_CIXSCMI_ACPI_H_
#define _DEV_ACPI_CIXSCMI_ACPI_H_

#include <sys/types.h>
#include <dev/acpi/acpireg.h>
#include <dev/ic/scmi_smt_diag.h>

struct cixscmi_clock67;
struct cixscmi_clock67_result {
	bool helper_called;
	bool trace_valid;
	bool ack_after_valid;
	bool service_usable;
	bool wire_poisoned;
	unsigned int remaining;
	int helper_return;
	int decode_error;
	int ack_error;
	uint32_t ack_before;
	uint32_t ack_after;
	struct scmi_smt_diag_trace trace;
};

/*
 * Private backend for cixclk. Consumers must acquire the shared logical claim
 * through cixclk_acpi_get_native. Only selector128 publishes this boot-long
 * service. No detach, release, retry, rate setter or arbitrary ID is supported.
 * Setup requires cold boot; published operations require thread context.
 * Get performs no AML or MMIO. It checks both actual handles in one ACPI root.
 * Invalid arguments/context leave outputs unchanged. A valid operation reports
 * failures explicitly; get_rate never changes its rate output on failure.
 * remaining counts conservative helper calls, not exact firmware tokens. One
 * rollback slot is protected while enabled/uncertain. A failed final disable
 * can exhaust it permanently. Keep ownership after any attempted control error.
 * Lock order: cixclk sleep mutex, then this service's sleep mutex. No reentry.
 */
int cixscmi_acpi_clock67_get(ACPI_HANDLE, ACPI_HANDLE,
    struct cixscmi_clock67 **);
int cixscmi_acpi_clock67_get_rate(struct cixscmi_clock67 *, uint64_t *,
    struct cixscmi_clock67_result *);
int cixscmi_acpi_clock67_config_set(struct cixscmi_clock67 *, uint32_t,
    struct cixscmi_clock67_result *);

/*
 * Cache only. Selector256 publishes both checked UART functional rates once.
 * No AML, mailbox or UART access occurs here. Thread context is required.
 * Only IDs264/266 are supported. Errors leave out unchanged. Zero is a valid
 * cached observation; a consumer must reject unusable divisor inputs itself.
 */
int cixscmi_acpi_uart_rate(ACPI_HANDLE, uint32_t, uint64_t *);

#endif /* _DEV_ACPI_CIXSCMI_ACPI_H_ */
