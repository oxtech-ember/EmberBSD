/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _ARM_BROADCOM_BCM2712_DISPLAY_HVSVAR_H_
#define _ARM_BROADCOM_BCM2712_DISPLAY_HVSVAR_H_

#include <sys/types.h>

enum bcm2712_hvs_variant {
	BCM2712_HVS_C0 = 0x53,
	BCM2712_HVS_D0 = 0x54,
};

/* Metadata for existing caller-owned HVS mappings and UPM reservations. */
struct bcm2712_hvs_frame {
	enum bcm2712_hvs_variant variant;
	uint64_t dma_base;
	uint64_t dma_span;
	uint32_t upm_offset;
	uint32_t upm_length;
	unsigned int upm_slot;
};

/* Nine plane words and one channel terminator. Numeric words, not MMIO. */
#define BCM2712_HVS_DLIST_WORDS 10
struct bcm2712_hvs_dlist {
	uint32_t words[BCM2712_HVS_DLIST_WORDS];
};

/*
 * Encode one 1920x1080 linear XRGB8888 frame, pitch 7680, offset 0.
 * NULL returns EINVAL first. Unknown variants return EOPNOTSUPP next.
 * Invalid DMA or UPM metadata returns EINVAL. Errors do not change output.
 * DMA covers the full supplied span below 2^36; address 0 is permitted.
 * UPM uses byte units and hardware slot 0..31, not an allocator handle.
 * The caller owns storage, serialization, mappings, coherency and reservations.
 * This function does not prove those properties or acquire their lifetime.
 * It does not publish, enable, stop or drain HVS. HVS changes context in its
 * SRAM copy; never apply this initial template to an active list.
 */
int bcm2712_hvs_encode_1080p60(const struct bcm2712_hvs_frame *,
    struct bcm2712_hvs_dlist *);

#endif /* _ARM_BROADCOM_BCM2712_DISPLAY_HVSVAR_H_ */
