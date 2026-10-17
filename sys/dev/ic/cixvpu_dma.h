/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: original stopped-only native DMA allocation interface, AI-assisted. */
#ifndef _DEV_IC_CIXVPU_DMA_H_
#define _DEV_IC_CIXVPU_DMA_H_

#include <sys/types.h>
#include <sys/bus.h>
#include <dev/ic/cixvpu_fw_plan.h>

struct cixvpu_dma;
struct cixvpu_dma_page {
	void *kva;
	bus_addr_t bus_address;
	struct cixvpu_fw_plan_page plan_page;
};

/*
 * ALLOCATION ONLY: all pages remain CPU-owned and never device-visible.
 * Never disclose these addresses to hardware. free is FORBIDDEN after any
 * hardware exposure, even if firmware was not started. A future runtime must
 * add an immutable/device-visible epoch before integration, and require reset,
 * bus drain and callback quiescence before unloading or freeing exposed pages.
 * This module does not implement that lifetime or DMA ownership transfer.
 *
 * Thread context only; the caller serializes all access, including free.
 * Only PAGE_SIZE == 4096 is supported. alloc copies and validates the plan,
 * chooses dmat64 when present (otherwise dmat), and acquires a 40-bit child
 * tag. Parent tags remain borrowed. Each unique backing page owns one native
 * allocation/map, including shared pages and MMU tables. Distinct owners
 * have disjoint loaded bus ranges. Mapping aliases do
 * not allocate owners. All CPU pages are zeroed; no firmware is copied, PTE is
 * built, bus_dmamap_sync is performed or device operation is issued. A bounce
 * buffer may contain different bytes until a future PREWRITE; the loaded bus
 * memory is not prepared for a device by this allocator.
 *
 * page returns a stopped-only CPU view and validated loaded DMA bus address.
 * The common/print CPU mappings use the BUS_DMA_COHERENT hint; this neither
 * asserts device snooping nor changes ACPI _CCA or the parent's DMA flags.
 * Future device handoff requires an explicit sync/ownership protocol.
 *
 * alloc requires *out == NULL. All outputs remain unchanged on error.
 * free accepts a live allocation and sets the caller's pointer NULL on success.
 * Other copies of that pointer and all page views then become invalid.
 */
int cixvpu_dma_alloc(bus_dma_tag_t, bus_dma_tag_t,
    const struct cixvpu_fw_plan *, struct cixvpu_dma **);
int cixvpu_dma_page(struct cixvpu_dma *, unsigned int,
    struct cixvpu_dma_page *);
int cixvpu_dma_free(struct cixvpu_dma **);

#endif /* _DEV_IC_CIXVPU_DMA_H_ */
