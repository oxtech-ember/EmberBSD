/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 The EmberBSD Project. */
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "../espressif/esp_axi_gdma_desc.c"

#define CHECK_STATUS(expression, expected) do { \
	int status = (expression); \
	assert(status == (expected)); \
} while (0)

int
main(void)
{
	struct esp_axi_gdma_desc d[3], saved[3];
	bus_dma_segment_t s[2] = {
		{ .ds_addr = 0x50001000, .ds_len = 4096 },
		{ .ds_addr = 0x50010000, .ds_len = 64 }
	};
	size_t used = 0, bytes = 0;

	assert(sizeof(d[0]) == 16);
	CHECK_STATUS(esp_axi_gdma_desc_build(d, 3, 0x50000000, s, 2,
	    &used), 0);
	assert(used == 3 && d[0].address == 0x50001000 &&
	    d[0].control == (ESP_AXI_GDMA_DESC_OWNER | 4032) &&
	    d[0].next == 0x50000010);
	assert(d[1].address == 0x50001fc0 &&
	    d[1].control == (ESP_AXI_GDMA_DESC_OWNER | 64) &&
	    d[1].next == 0x50000020);
	assert(d[2].address == 0x50010000 && d[2].next == 0 &&
	    (d[2].address & 63) == 0 &&
	    (d[0].control & ESP_AXI_GDMA_DESC_SIZE_MASK) % 64 == 0 &&
	    (d[1].control & ESP_AXI_GDMA_DESC_SIZE_MASK) % 64 == 0);
	memcpy(saved, d, sizeof(d));
	CHECK_STATUS(esp_axi_gdma_desc_build(d, 2, 0x50000000, s, 2,
	    &used), EFBIG);
	assert(memcmp(saved, d, sizeof(d)) == 0);
	s[0].ds_addr++;
	CHECK_STATUS(esp_axi_gdma_desc_build(d, 3, 0x50000000, s, 2,
	    &used), EINVAL);
	s[0].ds_addr = 0xfffffffc;
	s[0].ds_len = 8;
	CHECK_STATUS(esp_axi_gdma_desc_build(d, 3, 0x50000000, s, 2,
	    &used), EINVAL);
	s[0].ds_addr = 0x50001000;
	s[0].ds_len = 4096;
	CHECK_STATUS(esp_axi_gdma_desc_build(d, 3, 0xfffffff0, s, 2,
	    &used), EFBIG);
	/* A short EOF retires only the completed prefix; untouched tail stays DMA-owned. */
	CHECK_STATUS(esp_axi_gdma_desc_build(d, 3, 0x50000000, s, 2,
	    &used), 0);
	d[0].control = 4032 | (100U << 12) | ESP_AXI_GDMA_DESC_SUC_EOF;
	CHECK_STATUS(esp_axi_gdma_desc_result(d, 3, &bytes), 0);
	assert(bytes == 100);
	assert((d[1].control & ESP_AXI_GDMA_DESC_OWNER) != 0 &&
	    (d[2].control & ESP_AXI_GDMA_DESC_OWNER) != 0);
	d[0].control = 4032 | (4032U << 12);
	d[1].control = 64 | (64U << 12) | ESP_AXI_GDMA_DESC_SUC_EOF;
	CHECK_STATUS(esp_axi_gdma_desc_result(d, 3, &bytes), 0);
	assert(bytes == 4096);
	assert(!esp_axi_gdma_desc_owned_by_cpu(d, 3));
	d[2].control = 64 | (64U << 12) | ESP_AXI_GDMA_DESC_SUC_EOF;
	d[0].control &= ~ESP_AXI_GDMA_DESC_SUC_EOF;
	d[1].control &= ~ESP_AXI_GDMA_DESC_SUC_EOF;
	CHECK_STATUS(esp_axi_gdma_desc_result(d, 3, &bytes), 0);
	assert(bytes == 4160 && esp_axi_gdma_desc_owned_by_cpu(d, 3));
	d[2].control |= ESP_AXI_GDMA_DESC_ERR_EOF;
	CHECK_STATUS(esp_axi_gdma_desc_result(d, 3, &bytes), EIO);
	d[2].control &= ~ESP_AXI_GDMA_DESC_ERR_EOF;
	d[2].control |= ESP_AXI_GDMA_DESC_OWNER;
	CHECK_STATUS(esp_axi_gdma_desc_result(d, 3, &bytes), EIO);
	d[2].control = 64 | (65U << 12) | ESP_AXI_GDMA_DESC_SUC_EOF;
	CHECK_STATUS(esp_axi_gdma_desc_result(d, 3, &bytes), EIO);
	return 0;
}
