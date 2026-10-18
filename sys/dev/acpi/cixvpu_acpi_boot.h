/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: original bounded manual firmware boot epoch, AI-assisted. */
#ifndef _DEV_ACPI_CIXVPU_ACPI_BOOT_H_
#define _DEV_ACPI_CIXVPU_ACPI_BOOT_H_

#include <dev/acpi/cixvpu_acpi_control.h>

struct cixvpu_acpi_boot;
struct cixvpu_prepared;

enum cixvpu_boot_stage {
	CIXVPU_BOOT_CREATED, CIXVPU_BOOT_SYNC, CIXVPU_BOOT_CLAIM,
	CIXVPU_BOOT_IRQ, CIXVPU_BOOT_IDENTITY, CIXVPU_BOOT_REPAIR,
	CIXVPU_BOOT_SETUP, CIXVPU_BOOT_EXPOSE, CIXVPU_BOOT_START,
	CIXVPU_BOOT_RECEIVE, CIXVPU_BOOT_HELD,
	CIXVPU_BOOT_FRAME_OPEN, CIXVPU_BOOT_FRAME_SETUP, CIXVPU_BOOT_FRAME_RUN,
	CIXVPU_BOOT_FRAME_COPY, CIXVPU_BOOT_FRAME_COMPLETE
};
struct cixvpu_boot_report {
	struct cixvpu_acpi_report control;
	struct cixvpu_hw_profile profile;
	ACPI_STATUS core_status[4], core_cleanup[4];
	uint32_t core_attempted, core_released;
	uint32_t repair_pass, repair_enable, repair_busy;
	uint32_t terminate_reads, visits, messages, irq_seen, irq_unexpected;
	uint32_t last_offset, last_value;
	uint32_t rpc_state, rpc_callid, rpc_size;
	uint32_t rpc_alloc_size, rpc_alloc_max_size;
	uint32_t rpc_resize_address, rpc_resize_new_size;
	uint32_t rpc_commits, rpc_notifications;
	uint32_t frame_visits, frame_messages, frame_received_bytes;
	uint32_t frame_publications, frame_notifications;
	uint32_t frame_action, frame_option, frame_output_returns, frame_output_bytes;
	uint32_t frame_input_flags, frame_output_flags;
	uint8_t rpc_alloc_region, rpc_alloc_log2_alignment;
	enum cixvpu_boot_stage stage, last_stage;
	int error, cleanup_error, rpc_header_error;
	bool started, moved, exposed, held, switched_in, pong, prepared_retained;
	bool irq_registered, irq_masked;
	bool rpc_alloc_valid, rpc_resize_valid;
	bool rpc_alloc_replied, rpc_resize_replied, rpc_second_alloc_replied;
	bool rpc_header_valid; /* One-copy observation, not transaction stability. */
	bool frame_input_accepted, frame_output_eof, frame_complete;
};
/*
 * Process context, external serialization, one manual boot-long object.
 * create copies attach arguments/resources and performs no hardware operation.
 * out must name valid writable storage and initially be NULL.
 * Errors preserve it.
 * once accepts a live exclusive SEALED prepare_boot_cpu owner and handle slot,
 * with no old borrows. It consumes the attempt, calls initial sync before MOVE
 * or resources, and preserves the input handle on sync error without cleanup.
 * Sync success immediately MOVEs the owner; the caller must not use aliases.
 * mountroot calls this at cold==0. Mode6 retains its cold==1 policy.
 * Before capability only, safe rollback can free the never-exposed owner.
 * Every capability-success outcome retains pages, mappings, claims and handler
 * forever, even if no root MMIO write occurs. Terminal outcomes mask our own
 * exclusive IRQ once. Masking is not drain or reclamation. No destructor/retry,
 * suspend/resume or arbitrary register/protocol API exists. Only create_frame
 * selects the separate, fixed one-frame command policy.
 * Reports contain raw/advisory observations; API success does not prove repair.
 */
int cixvpu_acpi_boot_create(device_t, const struct acpi_attach_args *,
    const struct cixvpu_resources *, struct cixvpu_acpi_boot **);
/* Separate manual policy for two exact ALLOCs, one 512 KiB RESIZE and DEBUG. */
int cixvpu_acpi_boot_create_rpc(device_t, const struct acpi_attach_args *,
    const struct cixvpu_resources *, struct cixvpu_acpi_boot **);
/*
 * One successful create_frame per boot. Constructor failures unwind host nodes
 * and storage. Successful construction consumes this claim even before once.
 * It preallocates a 256 KiB host result and removable read-only sysctl nodes.
 * No later DMA allocation or held-owner reversal is permitted. Firmware, slots
 * and resources still require their independent reviewed qualification.
 * After terminal freeze, hw.<device>.hevc_frame exposes only the host copy.
 * frame_status is 96 bytes: version1 and length96 followed by twenty-two LE32
 * numeric fields. It has no struct padding, kernel address or DMA getter.
 * Native discovery sanitizes owned child metadata. It supports current ABI
 * flags with node version zero only.
 * Nonzero node versions return ENOTSUP. Generic hw/root metadata
 * remain the native sysctl ABI; emulation-overlay discovery is unsupported.
 * The callback permits partial copy; the user wrapper returns ENOMEM and the
 * required length on a successful short copy. EFAULT preserves prior length.
 */
#define CIXVPU_BOOT_FRAME_API 1

#define CIXVPU_BOOT_FRAME_EXPORT_API 1
#define CIXVPU_BOOT_FRAME_BYTES 262144U
int cixvpu_acpi_boot_create_frame(device_t, const struct acpi_attach_args *,
    const struct cixvpu_resources *, struct cixvpu_acpi_boot **);
int cixvpu_acpi_boot_once(struct cixvpu_acpi_boot *, struct cixvpu_prepared **);
int cixvpu_acpi_boot_report(const struct cixvpu_acpi_boot *,
    struct cixvpu_boot_report *);
#endif
