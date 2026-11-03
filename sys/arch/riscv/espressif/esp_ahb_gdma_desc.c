/* $NetBSD$ */
/* SPDX-FileCopyrightText: 2026 The EmberBSD Project */
/* SPDX-License-Identifier: BSD-2-Clause */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/bus.h>
#include <sys/errno.h>

#include "esp_ahb_gdma_desc.h"

/*
 * Each input segment is one DMA block. A block may span several descriptors;
 * EOF is attached to its last descriptor when block interrupts are requested.
 * Validate all segments before modifying the caller's descriptor area.
 */
int
esp_ahb_gdma_desc_build(struct esp_ahb_gdma_desc *desc, size_t capacity,
    uint64_t base, const bus_dma_segment_t *segs, size_t nsegs, bool write,
    bool block_irq, bool block_multi, size_t *used)
{
	uint64_t addr, len;
	size_t i, n, count;

	if (desc == NULL || segs == NULL || used == NULL || nsegs == 0 ||
	    base < AHB_GDMA_ADDR_MIN || base > AHB_GDMA_ADDR_MAX ||
	    (base & 3) != 0 || (block_multi && !block_irq))
		return EINVAL;

	count = 0;
	for (i = 0; i < nsegs; i++) {
		addr = segs[i].ds_addr;
		len = segs[i].ds_len;
		if (addr < AHB_GDMA_ADDR_MIN || addr > AHB_GDMA_ADDR_MAX ||
		    len == 0 || len - 1 > AHB_GDMA_ADDR_MAX - addr ||
		    (addr & 3) != 0 || (len & 3) != 0)
			return EINVAL;
		n = len / AHB_DESC_MAX_BYTES +
		    (len % AHB_DESC_MAX_BYTES != 0);
		if (n > capacity - count)
			return EFBIG;
		count += n;
	}
	if (count == 0 || count > (AHB_GDMA_ADDR_MAX - base + 1) /
	    sizeof(*desc))
		return EFBIG;

	n = 0;
	for (i = 0; i < nsegs; i++) {
		addr = segs[i].ds_addr;
		len = segs[i].ds_len;
		while (len != 0) {
			const uint32_t bytes = len > AHB_DESC_MAX_BYTES ?
			    AHB_DESC_MAX_BYTES : (uint32_t)len;
			const bool last_block_desc = bytes == len;
			const bool eof = block_irq ? last_block_desc :
			    (i + 1 == nsegs && last_block_desc);
			uint32_t control = bytes | AHB_DESC_OWNER;

			if (write)
				control |= bytes << AHB_DESC_LENGTH_SHIFT;
			if (eof)
				control |= AHB_DESC_SUC_EOF;
			desc[n].control = control;
			desc[n].buffer = (uint32_t)addr;
			desc[n].next = n + 1 == count ?
			    (block_multi ? (uint32_t)base : 0) :
			    (uint32_t)(base + (n + 1) * sizeof(*desc));
			addr += bytes;
			len -= bytes;
			n++;
		}
	}
	*used = count;
	return 0;
}

/*
 * The AHB DONE interrupt is per descriptor, including in a cyclic chain.
 * Period callbacks and finite completion are driven only by successful EOF.
 */
int
esp_ahb_gdma_advance_eof(size_t *last, const bool *eof, size_t ndesc,
    size_t index, bool cyclic, size_t *periods, bool *complete)
{
	size_t i, count = 0;

	if (last == NULL || eof == NULL || periods == NULL || complete == NULL ||
	    ndesc == 0 || *last >= ndesc || index >= ndesc || !eof[index])
		return EINVAL;
	if (index != *last) {
		for (i = (*last + 1) % ndesc;; i = (i + 1) % ndesc) {
			if (eof[i])
				count++;
			if (i == index)
				break;
		}
	}
	*last = index;
	*periods = count;
	*complete = !cyclic && index == ndesc - 1;
	return 0;
}
