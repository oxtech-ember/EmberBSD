/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted; owned native NPU DMA lifecycle. */
#ifndef _SUN60I_A733_NPU_DMA_H
#define _SUN60I_A733_NPU_DMA_H
#include <sys/bus.h>

struct sun60i_a733_npu_dma;
struct sun60i_a733_npu_dma_info {
	uint64_t mtlb_address;
	uint32_t pd_entry, command_virtual;
	size_t command_bytes;
	bool retained;
};

/*
 * The caller supplies a trusted, stable kernel command image and serializes
 * the context. The image has 8..4096 bytes and contains whole 8-byte commands.
 * This fixed profile owns 24 KiB: root 4 KiB, STLB 16 KiB, command 4 KiB.
 * Only the command page is mapped, read-only, at device VA 0x01001000.
 * No function validates command semantics or proves clock/power readiness.
 * Prepare performs no MMIO, IRQ setup or cache sync. Failure preserves *out.
 */
int sun60i_a733_npu_dma_prepare(bus_dma_tag_t, const void *, size_t,
    struct sun60i_a733_npu_dma **);
int sun60i_a733_npu_dma_info(const struct sun60i_a733_npu_dma *,
    struct sun60i_a733_npu_dma_info *);
/* Mark retained before the first sync. Call before hardware publication. */
int sun60i_a733_npu_dma_retain(struct sun60i_a733_npu_dma *);
/* Retained storage cannot be destroyed, including after errors or timeouts. */
int sun60i_a733_npu_dma_destroy(struct sun60i_a733_npu_dma *);
#endif
