/* $NetBSD$ */
/* SPDX-FileCopyrightText: 2024-2026 Espressif Systems (Shanghai) CO LTD */
/* SPDX-FileCopyrightText: 2026 The EmberBSD Project */
/* SPDX-License-Identifier: Apache-2.0 */

#ifndef _RISCV_ESPRESSIF_ESP_AXI_GDMA_DESC_H_
#define _RISCV_ESPRESSIF_ESP_AXI_GDMA_DESC_H_

#include <sys/types.h>
#include <sys/bus.h>

/* ESP-IDF v6.1 fff9895c: components/esp_hal_dma/include/hal/dma_types.h. */
struct esp_axi_gdma_desc {
	uint32_t control;
	uint32_t address;
	uint32_t next;
	uint32_t pad;
};

#define ESP_AXI_GDMA_DESC_MAX_BYTES 4032U
#define ESP_AXI_GDMA_DESC_SIZE_MASK 0xfffU
#define ESP_AXI_GDMA_DESC_LENGTH_SHIFT 12
#define ESP_AXI_GDMA_DESC_ERR_EOF (1U << 28)
#define ESP_AXI_GDMA_DESC_SUC_EOF (1U << 30)
#define ESP_AXI_GDMA_DESC_OWNER (1U << 31)

int esp_axi_gdma_desc_build(struct esp_axi_gdma_desc *, size_t, uint64_t,
    const bus_dma_segment_t *, size_t, size_t *);
int esp_axi_gdma_desc_result(const struct esp_axi_gdma_desc *, size_t,
    size_t *);
int esp_axi_gdma_desc_owned_by_cpu(const struct esp_axi_gdma_desc *, size_t);

#endif
