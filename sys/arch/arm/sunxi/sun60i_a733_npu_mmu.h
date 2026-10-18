/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted; independent VIP MMU32 data encoding. */
#ifndef _SUN60I_A733_NPU_MMU_H
#define _SUN60I_A733_NPU_MMU_H

#define A733_NPU_MMU_MTLB_BYTES 1024
#define A733_NPU_MMU_STLB_BYTES 16384
#define A733_NPU_MMU_MAX_PAGES 64
#define A733_NPU_MMU_WRITABLE 1

struct sun60i_a733_npu_mmu_page {
	uint64_t physical;
	uint32_t flags;
};

struct sun60i_a733_npu_mmu_plan {
	uint64_t mtlb_physical, stlb_physical;
	uint32_t virtual_base;
	const struct sun60i_a733_npu_mmu_page *pages;
	size_t page_count;
};

/*
 * CPU-only VA32/PA32 profile: one 16 MiB slot and two unmapped edge pages.
 * The caller supplies trusted kernel pointers and owns the physical pages.
 * This interface does not allocate memory or prove DMA or hardware readiness.
 * Validation failure leaves both output images unchanged. Success writes only
 * MTLB_BYTES and STLB_BYTES. Extra output capacity is not part of either image.
 */
int sun60i_a733_npu_mmu_build(const struct sun60i_a733_npu_mmu_plan *,
    void *, size_t, void *, size_t);
/* PD-mode root: 4 KiB aligned and below 4 GiB. Failure preserves the output. */
int sun60i_a733_npu_mmu_pd_entry(uint64_t, uint32_t *);

#endif
