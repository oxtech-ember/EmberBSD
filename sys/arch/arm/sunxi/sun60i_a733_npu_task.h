/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted; fixed native NN lifetime interface. */
#ifndef _SUN60I_A733_NPU_TASK_H
#define _SUN60I_A733_NPU_TASK_H
#include <arm/sunxi/sun60i_a733_npu_dma.h>
#include <arm/sunxi/sun60i_a733_npu_run.h>

struct clk;
struct sun60i_a733_npu_task;
struct sun60i_a733_npu_task_status {
	uint32_t prepared, attempted, retained, result, release_error;
	struct sun60i_a733_npu_dma_task_info memory;
	struct sun60i_a733_npu_run_status run;
	struct sun60i_a733_npu_dma_task_report compare;
};
/*
 * The caller serializes this permanent context. Prepare builds only the
 * fixed NN image and 144-byte stream. References use bounded heap storage.
 * It does not perform MMIO, IRQ setup or sync. Normal failure preserves *out.
 * If cleanup fails, failure returns the kept context in *out, with prepared == 0
 * and release_error. The caller must keep this pointer and must not run it.
 * There is no arbitrary tensor, command, address or output-readback API.
 */
int sun60i_a733_npu_task_prepare(bus_dma_tag_t,
    struct sun60i_a733_npu_task **);
/* A cold one-shot run. Caller must first revoke legacy MMIO readiness. */
int sun60i_a733_npu_task_run(struct sun60i_a733_npu_task *, bus_space_tag_t,
    struct clk *, int, const void *, int, const char *);
/* Copies cached metadata. It performs no MMIO or DMA data read. */
void sun60i_a733_npu_task_status(struct sun60i_a733_npu_task *,
    struct sun60i_a733_npu_task_status *);
/* Any publication attempt keeps the context, references, maps and IRQ. */
int sun60i_a733_npu_task_destroy(struct sun60i_a733_npu_task *);
#endif
