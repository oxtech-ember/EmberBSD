/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: original prepared-owner DMA coupling, AI-assisted. */
#ifndef _DEV_IC_CIXVPU_DMA_PRIVATE_H_
#define _DEV_IC_CIXVPU_DMA_PRIVATE_H_

#include <dev/ic/cixvpu_dma.h>

struct cixvpu_prepared;

/*
 * Internal allocator/preparer interface. The owner is the actual live prepared
 * wrapper. Calls are externally serialized and require thread context.
 * seal follows the preparer's last fallible CPU/table check. A failed seal
 * changes no state; success must be followed only by the ownership move.
 * No mutable view or address is returned by these operations.
 */
int cixvpu_dma_seal(struct cixvpu_dma *, const struct cixvpu_prepared *);
int cixvpu_dma_sync_initial(struct cixvpu_dma *,
    const struct cixvpu_prepared *, const struct cixvpu_fw_plan *);
int cixvpu_dma_free_prepared(struct cixvpu_dma **,
    const struct cixvpu_prepared *);

/*
 * Private irreversible exposure guard, used by the typed root capability.
 * Publication must mark before returning an address, even if
 * hardware publication later fails. This transition is irreversible.
 */
int cixvpu_dma_mark_exposed(struct cixvpu_dma *,
    const struct cixvpu_prepared *);

/*
 * Fixed boot MESSAGE snapshot. No shared memory or map view escapes.
 * boot_output identifies retained-object validation faults separately from
 * caller output/alias errors. Its fault output is private stack storage.
 * The preparer designates only its retained snapshot for boot_snapshot.
 */
struct cixvpu_dma_boot_snapshot {
	uint8_t message[CIXVPU_FW_PAGE_SIZE];
	uint8_t rpc_header[12], rpc_alloc[12], rpc_resize[8];
	bool rpc_valid, rpc_alloc_valid, rpc_resize_valid;
};
int cixvpu_dma_boot_output(struct cixvpu_dma *,
    const struct cixvpu_prepared *, const void *, size_t, bool *);
int cixvpu_dma_boot_snapshot(struct cixvpu_dma *,
    const struct cixvpu_prepared *, const struct cixvpu_fw_plan *,
    struct cixvpu_dma_boot_snapshot *);
/* Closed DEBUGPARAM only; success retains the original observed header. */
int cixvpu_dma_boot_debugparam(struct cixvpu_dma *,
    const struct cixvpu_prepared *, const struct cixvpu_fw_plan *,
    struct cixvpu_dma_boot_snapshot *);
enum cixvpu_dma_rpc_result {
	CIXVPU_DMA_RPC_IDLE,
	CIXVPU_DMA_RPC_PENDING,
	CIXVPU_DMA_RPC_ALLOC_REPLIED,
	CIXVPU_DMA_RPC_DEBUG_REPLIED
};
int cixvpu_dma_boot_rpc_step(struct cixvpu_dma *,
    const struct cixvpu_prepared *, const struct cixvpu_fw_plan *,
    struct cixvpu_dma_boot_snapshot *, enum cixvpu_dma_rpc_result *);
int cixvpu_dma_boot_snapshot_rpc(struct cixvpu_dma *,
    const struct cixvpu_prepared *, const struct cixvpu_fw_plan *,
    struct cixvpu_dma_boot_snapshot *);
int cixvpu_dma_boot_ack(struct cixvpu_dma *,
    const struct cixvpu_prepared *, const struct cixvpu_fw_plan *, uint16_t);

#endif
