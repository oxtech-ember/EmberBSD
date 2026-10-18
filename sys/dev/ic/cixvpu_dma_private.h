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
 * Source-only exposure guard; no production caller or L0 capability yet.
 * A future publication API must mark before returning an address, even if
 * hardware publication later fails. This transition is irreversible.
 */
int cixvpu_dma_mark_exposed(struct cixvpu_dma *,
    const struct cixvpu_prepared *);

#endif
