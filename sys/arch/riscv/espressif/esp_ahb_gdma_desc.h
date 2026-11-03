/* $NetBSD$ */
/* SPDX-FileCopyrightText: 2026 The EmberBSD Project */
/* SPDX-License-Identifier: BSD-2-Clause */

#ifndef _RISCV_ESPRESSIF_ESP_AHB_GDMA_DESC_H_
#define _RISCV_ESPRESSIF_ESP_AHB_GDMA_DESC_H_

#include <sys/types.h>
#include <sys/bus.h>

/* ESP-IDF 6.1 dma_types.h, 4-byte-aligned AHB descriptor. */
struct esp_ahb_gdma_desc {
	uint32_t control;
	uint32_t buffer;
	uint32_t next;
};

#define AHB_DESC_MAX_BYTES	4092U
#define AHB_DESC_SIZE_MASK	0xfffU
#define AHB_DESC_LENGTH_SHIFT	12
#define AHB_DESC_ERR_EOF	(1U << 28)
#define AHB_DESC_SUC_EOF	(1U << 30)
#define AHB_DESC_OWNER		(1U << 31)
#define AHB_GDMA_ADDR_MIN	0x2f000000ULL
#define AHB_GDMA_ADDR_MAX	0x53ffffffULL

int esp_ahb_gdma_desc_build(struct esp_ahb_gdma_desc *, size_t, uint64_t,
	const bus_dma_segment_t *, size_t, bool, bool, bool, size_t *);
int esp_ahb_gdma_advance_eof(size_t *, const bool *, size_t, size_t,
	bool, size_t *, bool *);

#endif
