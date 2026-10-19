/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _ARM_BROADCOM_BCM2712_MMU4_TABLEVAR_H_
#define _ARM_BROADCOM_BCM2712_MMU4_TABLEVAR_H_

#include <sys/types.h>

#define BCM2712_MMU4_PAGE_BYTES	4096U
#define BCM2712_MMU4_FRAME_PAGES	2025U

/* Fixed aperture [0, 256 MiB), frame IOVA 0, read-only 4 KiB leaves. */
struct bcm2712_mmu4_pages {
	uint32_t debug;
	uint64_t top_pa;
	uint64_t l2_pa[2];
	uint64_t default_pa;
	uint64_t frame_pa[BCM2712_MMU4_FRAME_PAGES];
};

struct bcm2712_mmu4_image {
	uint8_t top[BCM2712_MMU4_PAGE_BYTES];
	uint8_t l2[2][BCM2712_MMU4_PAGE_BYTES];
	uint8_t default_page[BCM2712_MMU4_PAGE_BYTES];
};

/*
 * Supply accessible, correctly aligned, disjoint input and output objects.
 * No large input or image belongs on the kernel stack. All PAs identify full
 * 4 KiB physical pages below 2^36. Control pages must be nonzero, distinct and
 * disjoint from frame pages. Repeated immutable frame PAs, including 0, are
 * representable. These checks do not prove physical ownership or DMA mapping.
 *
 * Return positive BSD errno. Reject CPU-object range/overlap/alignment errors
 * with EINVAL before dereferencing input. Then reject unsupported DEBUG with
 * EOPNOTSUPP, PA alignment/aliases with EINVAL, and PA span overflow with
 * ERANGE.
 * Validate all input before changing any output byte. The caller must exclude
 * input mutation, output readers and device access until the function returns.
 * Success is error-atomic, not concurrent 16 KiB publication. It provides no
 * allocation, synchronization, MMIO, cache, translation or lifetime operation.
 */
int bcm2712_mmu4_encode(const struct bcm2712_mmu4_pages *,
    struct bcm2712_mmu4_image *);

#define BCM2712_MMU4_SCANOUT_SLOTS 3U
/* One fixed 8 MiB window at IOVA (slot + 1) * 8 MiB. */
struct bcm2712_mmu4_range_pages {
	uint32_t debug;
	uint64_t top_pa, l2_pa[2];
	uint64_t frame_pa[BCM2712_MMU4_FRAME_PAGES];
};

/* Two top entries and two L2 pages. No active table is modified. */
struct bcm2712_mmu4_range_image {
	uint8_t top[8];
	uint8_t l2[2][BCM2712_MMU4_PAGE_BYTES];
};

/*
 * Encode exactly 2025 read-only leaves for slot 0..2. The caller owns the
 * two L2 allocations and preserves all other top entries. Input/output must
 * be accessible, aligned and disjoint. All checks precede output writes.
 * Return positive errno. This operation provides no DMA sync or publication.
 */
int bcm2712_mmu4_encode_range(const struct bcm2712_mmu4_range_pages *,
    unsigned int, struct bcm2712_mmu4_range_image *);

#endif /* _ARM_BROADCOM_BCM2712_MMU4_TABLEVAR_H_ */
