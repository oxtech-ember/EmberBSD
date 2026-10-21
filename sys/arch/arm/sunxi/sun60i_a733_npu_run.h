/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted; one-shot owned native NPU run state. */
#ifndef _SUN60I_A733_NPU_RUN_H
#define _SUN60I_A733_NPU_RUN_H
#include <sys/bus.h>
#include <sys/mutex.h>
struct clk;
struct sun60i_a733_npu_dma;
struct sun60i_a733_npu_dma_task_reference;
#define A733_NPU_RUN_NREGS 18
#define A733_NPU_TASK_ID_WORDS 6
#define A733_NPU_TASK_ID_READ_ALL 0x3f
#define A733_NPU_TASK_CAP_HWDB 1
struct sun60i_a733_npu_run_status {
	uint32_t attempted, publication_attempted, retained, started, terminal;
	uint32_t snapshot_valid, step, error, last_readback, command_bytes;
	uint32_t raw[2][A733_NPU_RUN_NREGS];
	uint32_t irq_raw, irq_bits, irq_fault, irq_mask_readback;
	uint32_t stale[2], terminal_status, terminal_idle[2], terminal_dma[2];
	uint64_t irq_count, irq_baseline;
	uint32_t task_features[2][3], task_features_valid;
	uint32_t task_feature_read_mask[2];
	/* model, revision, product, ECO, customer, closing model. */
	uint32_t task_identity[2][A733_NPU_TASK_ID_WORDS];
	uint32_t task_identity_read_mask[2], task_identity_valid;
	uint32_t task_capability_source, task_profile_valid, task_int8_cores;
	uint32_t task_blt;
	uint32_t final_attempted, final_valid, model_pass;
	uint32_t final_mask[2], final_idle[2], final_dma[2], final_status;
};
struct sun60i_a733_npu_run {
	bus_space_tag_t bst;
	bus_space_handle_t bsh;
	void *ih;
	kmutex_t irq_lock;
	bool lock_initialized, mapped, armed, task_retained;
	uint32_t profile;
	const struct sun60i_a733_npu_dma *bound_dma;
	struct sun60i_a733_npu_run_status status;
};
/*
 * Caller serializes this permanent, initially zeroed state. The state and DMA
 * context must outlive every IRQ and write attempt. Only this finite native
 * stage can write NPU registers. No arbitrary offset/value API is exported.
 * Before publication, failure releases only the new mapping and IRQ cookie.
 * After publication is attempted, all storage survives errors until reboot.
 */
int sun60i_a733_npu_run_once(struct sun60i_a733_npu_run *, bus_space_tag_t,
    struct clk *, int, const void *, int, const char *,
    struct sun60i_a733_npu_dma *);
/*
 * Only the trusted fixed TASK coordinator calls this entry. References are
 * immutable heap storage. No caller-supplied stream or tensor is accepted by
 * the coordinator. This entry retains the native lifetime before first sync.
 */
int sun60i_a733_npu_run_task_once(struct sun60i_a733_npu_run *, bus_space_tag_t,
    struct clk *, int, const void *, int, const char *,
    struct sun60i_a733_npu_dma *,
    const struct sun60i_a733_npu_dma_task_reference *);
/*
 * Call only after the checked DMA comparison. Fresh typed readiness runs
 * outside the IRQ lock. The final live proof and model_pass publication run
 * in one IRQ critical section. There are no writes or retries in this proof.
 */
int sun60i_a733_npu_run_task_finish(struct sun60i_a733_npu_run *,
    const struct sun60i_a733_npu_dma *, struct clk *, int, const void *);
/*
 * Cache-only completion gate for the exact TASK DMA identity. FE completion
 * never grants permission to read TASK output. Caller serializes the context.
 * This does not replace the mandatory final live proof after comparison.
 */
bool sun60i_a733_npu_run_task_completed(struct sun60i_a733_npu_run *,
    const struct sun60i_a733_npu_dma *);
/* Copy cached state under its IRQ spin lock. This performs no hardware I/O. */
void sun60i_a733_npu_run_status(struct sun60i_a733_npu_run *,
    struct sun60i_a733_npu_run_status *);
#endif
