/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted; owned native NPU DMA lifecycle. */
#ifndef _SUN60I_A733_NPU_DMA_H
#define _SUN60I_A733_NPU_DMA_H
#include <sys/bus.h>

#define A733_NPU_DMA_PROFILE_FE 1
#define A733_NPU_DMA_PROFILE_TASK 2
#define A733_NPU_DMA_TASK_OUTPUT_POISON 0xa5

struct sun60i_a733_npu_dma;
struct sun60i_a733_npu_dma_info {
	uint64_t mtlb_address, stlb_address, command_address;
	uint32_t pd_entry, command_virtual;
	size_t command_bytes;
	bool retained;
	uint32_t profile;
};

struct sun60i_a733_npu_dma_task_info {
	struct sun60i_a733_npu_dma_info memory;
	uint64_t constants_address, input_address, output_address;
	uint32_t constants_virtual, input_virtual, output_virtual;
	size_t constants_bytes, input_bytes, output_bytes;
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
/*
 * The task profile owns 36 KiB in six disjoint low32 non-bounce maps.
 * Command, constants, and input pages are read-only at 0x01001000,
 * 0x01002000, and 0x01003000. The output page is writable at 0x01004000.
 * The two edge pages are unmapped. Constants and input have 1..4096 bytes.
 * Commands have 8..4096 bytes and contain whole 8-byte commands.
 * Inputs are trusted stable kernel spans. Failure preserves inputs and *out.
 * Prepare clears tails and poisons output. It does not sync or submit work.
 * Metadata access grants no output readback or completion capability.
 */
int sun60i_a733_npu_dma_task_prepare(bus_dma_tag_t, const void *, size_t,
    const void *, size_t, const void *, size_t, struct sun60i_a733_npu_dma **);
int sun60i_a733_npu_dma_task_info(const struct sun60i_a733_npu_dma *,
    struct sun60i_a733_npu_dma_task_info *);
/*
 * Replace only the command page of an FE profile before retain.
 * Failure preserves all bytes.
 */
int sun60i_a733_npu_dma_set_commands(struct sun60i_a733_npu_dma *,
    const void *, size_t);
/* Mark retained before the first sync. Call before hardware publication. */
int sun60i_a733_npu_dma_retain(struct sun60i_a733_npu_dma *);
/* Retained storage cannot be destroyed, including after errors or timeouts. */
int sun60i_a733_npu_dma_destroy(struct sun60i_a733_npu_dma *);
#endif
