/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: original bounded native VPU probe interface, AI-assisted. */
#ifndef _DEV_ACPI_CIXVPU_ACPI_CONTROL_H_
#define _DEV_ACPI_CIXVPU_ACPI_CONTROL_H_

#include <sys/types.h>
#include <sys/stdbool.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <dev/acpi/acpica.h>

struct acpi_attach_args;
struct cixvpu_resources;
struct cixclk;
struct cixreset;

struct cixvpu_acpi_report {
	uint64_t rate;
	uint32_t id, ncores, nlsid, svn;
	int error, cleanup_error;
	ACPI_STATUS acpi_status, cleanup_acpi_status;
	bool identity_valid, released;
	/* Valid means a decoded query response, including a valid zero rate. */
	bool rate_query_attempted, rate_valid;
};

/* Ordered raw observations only; valid does not prove fresh repair or rates. */
struct cixvpu_hw_profile {
	uint32_t strap_pin2, strap_pin0, pgctrl;
	uint32_t repair_pass, repair_enable, repair_busy, clkforce;
	uint32_t harvesting_mask, usable_core_mask;
	bool valid;
};

struct cixvpu_acpi_control {
	device_t dev;
	ACPI_HANDLE consumer, power;
	struct cixclk *clock;
	struct cixreset *reset, *rcsu;
	bus_space_tag_t bst;
	bus_space_handle_t bsh;
	bus_size_t span;
	bool started, power_attempted, clock_attempted, reset_safe, mapped;
	/* OFF released the native lease, but _STA did not confirm power off. */
	bool power_released_fault;
	struct cixvpu_acpi_report report;
	struct cixvpu_hw_profile profile;
	bus_space_handle_t profile_bsh;
	bus_size_t profile_span;
	bool profile_mapped;
};

/*
 * Thread context only. The caller serializes this embedded, boot-long object.
 * probe accepts a fresh zeroed object once. Query mode does not activate it.
 * Query mode records rate_query_attempted and rate_valid separately.
 * A valid decoded rate can be zero. Active mode does not query the rate.
 * Active mode pulses only the main reset, reads identity, then stops the block.
 * _STA confirms only the power bit; it does not prove repair or firmware ready.
 * Error and cleanup_error remain separate. Failed cleanup retains claims and
 * context. power_attempted records a pending rollback, not confirmed native
 * ownership. An ON error can precede lease insertion. If OFF cannot resolve
 * that attempt, retain the context conservatively for diagnosis. cleanup
 * retries pending controls; it never replays ON. An OFF readback
 * fault has no native power lease and requires diagnosis, not automatic retry.
 *
 * No DMA, interrupts, firmware loading or hot detach are supported. This
 * probe-only cleanup unmaps before reset. Do not reuse it for a DMA device.
 */
int cixvpu_acpi_control_probe(struct cixvpu_acpi_control *, device_t,
    const struct acpi_attach_args *, const struct cixvpu_resources *, bool);
/* Explicit native clock backend; active identity only, with no AML fallback. */
int cixvpu_acpi_control_probe_native(struct cixvpu_acpi_control *, device_t,
    const struct acpi_attach_args *, const struct cixvpu_resources *);
/*
 * Separate boot-only native profile. Seven additional fixed raw reads under
 * the same control claims; no repair, sync, DMA, IRQ or functional clock call.
 * Other resource locations or revisions return ENOTSUP for this local profile.
 * Cleanup and identity error semantics above remain in effect. A valid profile
 * can coexist with cleanup failure; it is neither atomic nor a repair proof.
 */
int cixvpu_acpi_control_probe_native_profile(struct cixvpu_acpi_control *,
    device_t, const struct acpi_attach_args *, const struct cixvpu_resources *);
int cixvpu_acpi_control_cleanup(struct cixvpu_acpi_control *);

#endif /* _DEV_ACPI_CIXVPU_ACPI_CONTROL_H_ */
