/* $NetBSD$ */
/* SPDX-FileCopyrightText: 2024-2026 Espressif Systems (Shanghai) CO LTD */
/* SPDX-FileCopyrightText: 2026 The EmberBSD Project */
/* SPDX-License-Identifier: Apache-2.0 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/bus.h>
#include <sys/errno.h>

#include "esp_axi_gdma_desc.h"

/*
 * Construct a bounded RX chain.  Validate the whole input before writing
 * descriptors, so rejected transfers do not leave a partially valid chain.
 */
int
esp_axi_gdma_desc_build(struct esp_axi_gdma_desc *desc, size_t capacity,
    uint64_t base, const bus_dma_segment_t *segs, size_t nsegs, size_t *used)
{
	uint64_t addr, len;
	size_t i, n, count;

	if (desc == NULL || segs == NULL || used == NULL || nsegs == 0 ||
	    (base & 7) != 0 || base > UINT32_MAX)
		return EINVAL;
	count = 0;
	for (i = 0; i < nsegs; i++) {
		addr = segs[i].ds_addr;
		len = segs[i].ds_len;
		if ((addr & 3) != 0 || (len & 3) != 0 || len == 0 ||
		    addr > UINT32_MAX || len > UINT32_MAX - addr + 1)
			return EINVAL;
		n = len / ESP_AXI_GDMA_DESC_MAX_BYTES +
		    (len % ESP_AXI_GDMA_DESC_MAX_BYTES != 0);
		if (n > capacity - count)
			return EFBIG;
		count += n;
	}
	if (count == 0 || count > (UINT32_MAX - base + 1) /
	    sizeof(*desc))
		return EFBIG;
	n = 0;
	for (i = 0; i < nsegs; i++) {
		addr = segs[i].ds_addr;
		len = segs[i].ds_len;
		while (len != 0) {
			const uint32_t size = len > ESP_AXI_GDMA_DESC_MAX_BYTES ?
			    ESP_AXI_GDMA_DESC_MAX_BYTES : (uint32_t)len;

			desc[n].control = size | ESP_AXI_GDMA_DESC_OWNER;
			desc[n].address = (uint32_t)addr;
			desc[n].next = n + 1 == count ? 0 :
			    (uint32_t)(base + (n + 1) * sizeof(*desc));
			desc[n].pad = 0;
			addr += size;
			len -= size;
			n++;
		}
	}
	*used = count;
	return 0;
}

/* Call after DMA ownership and descriptor cache sync have completed. */
int
esp_axi_gdma_desc_result(const struct esp_axi_gdma_desc *desc, size_t count,
    size_t *bytes)
{
	size_t i, total = 0;

	if (desc == NULL || bytes == NULL || count == 0)
		return EINVAL;
	for (i = 0; i < count; i++) {
		const uint32_t control = desc[i].control;
		const uint32_t size = control & ESP_AXI_GDMA_DESC_SIZE_MASK;
		const uint32_t len = (control >> ESP_AXI_GDMA_DESC_LENGTH_SHIFT) &
		    ESP_AXI_GDMA_DESC_SIZE_MASK;
		if ((control & (ESP_AXI_GDMA_DESC_ERR_EOF |
		    ESP_AXI_GDMA_DESC_OWNER)) != 0 || len > size ||
		    SIZE_MAX - total < len)
			return EIO;
		total += len;
		if ((control & ESP_AXI_GDMA_DESC_SUC_EOF) != 0) {
			*bytes = total;
			return 0;
		}
	}
	return EIO;
}

int
esp_axi_gdma_desc_owned_by_cpu(const struct esp_axi_gdma_desc *desc,
    size_t count)
{
	size_t i;

	if (desc == NULL || count == 0)
		return 0;
	for (i = 0; i < count; i++) {
		if ((desc[i].control & ESP_AXI_GDMA_DESC_OWNER) != 0)
			return 0;
	}
	return 1;
}
