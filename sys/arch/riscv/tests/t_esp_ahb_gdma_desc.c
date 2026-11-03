/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 The EmberBSD Project. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "../espressif/esp_ahb_gdma_desc.c"
#include "../espressif/esp_ahb_gdma_state.h"
#include "../espressif/esp_i2s_state.h"

#define CHECK_STATUS(expression, expected) do { \
	int status = (expression); \
	assert(status == (expected)); \
} while (0)

int
main(void)
{
	struct esp_ahb_gdma_desc d[8], saved[8];
	bus_dma_segment_t blocks[3] = {
		{ .ds_addr = 0x50001000, .ds_len = 32 },
		{ .ds_addr = 0x50002000, .ds_len = 64 },
		{ .ds_addr = 0x50003000, .ds_len = 96 },
	};
	bus_dma_segment_t split[2] = {
		{ .ds_addr = 0x50004000, .ds_len = 8192 },
		{ .ds_addr = 0x50008000, .ds_len = 32 },
	};
	size_t used = 0;
	bool eof[4] = { true, false, true, true };
	bool complete;
	size_t last, periods;

	assert(sizeof(d[0]) == 12);
	assert(esp_i2s_dma_alignment(4) == 64);
	assert(esp_i2s_dma_alignment(64) == 64);
	assert(esp_i2s_dma_alignment(128) == 128);
	/* The error callback can read terminal status before its own drain. */
	assert(esp_ahb_gdma_error_ready(true, false, EIO));
	assert(!esp_ahb_gdma_error_ready(false, false, EIO));
	assert(!esp_ahb_gdma_error_ready(true, true, EIO));
	assert(!esp_ahb_gdma_error_ready(true, false, 0));
	/* Stop suppresses queued callbacks; restart waits for stale work to drain. */
	assert(esp_ahb_gdma_dispatch_count(true, false, 3) == 3);
	assert(esp_ahb_gdma_dispatch_count(true, true, 3) == 0);
	assert(!esp_ahb_gdma_callbacks_drained(true, false, false));
	assert(!esp_ahb_gdma_callbacks_drained(false, false, true));
	assert(esp_ahb_gdma_callbacks_drained(false, false, false));
	assert(esp_ahb_gdma_halt_required(false, false, false, true));
	assert(!esp_ahb_gdma_halt_required(false, false, false, false));
	assert(!esp_ahb_gdma_can_start(true, false, false, false, true,
	    false, false, false, false));
	assert(!esp_ahb_gdma_can_start(true, false, false, false, false,
	    false, true, false, false));
	assert(esp_ahb_gdma_can_start(true, false, false, false, false,
	    false, false, false, false));
	/* A failed halt can leave an idle stream owning its DMA buffer. */
	assert(esp_i2s_halt_required(false, true, true));
	assert(esp_i2s_halt_required(true, false, false));
	assert(!esp_i2s_halt_required(false, false, false));
	/* Capture needs TX clocks even after playback has stopped. */
	assert(!esp_i2s_stop_tx_clock(false, true));
	assert(!esp_i2s_stop_tx_clock(true, false));
	assert(esp_i2s_stop_tx_clock(false, false));
	CHECK_STATUS(esp_ahb_gdma_desc_build(d, 8, 0x50000000, blocks, 3,
	    true, true, true, &used), 0);
	assert(used == 3);
	assert(d[0].buffer == 0x50001000 && d[0].next == 0x5000000c);
	assert(d[0].control == (32 | (32U << 12) | AHB_DESC_SUC_EOF |
	    AHB_DESC_OWNER));
	assert(d[1].control == (64 | (64U << 12) | AHB_DESC_SUC_EOF |
	    AHB_DESC_OWNER));
	assert(d[2].control == (96 | (96U << 12) | AHB_DESC_SUC_EOF |
	    AHB_DESC_OWNER) && d[2].next == 0x50000000);
	memcpy(saved, d, sizeof(d));
	CHECK_STATUS(esp_ahb_gdma_desc_build(d, 2, 0x50000000, blocks, 3,
	    true, true, true, &used), EFBIG);
	assert(memcmp(saved, d, sizeof(d)) == 0);
	CHECK_STATUS(esp_ahb_gdma_desc_build(d, 8, 0x50000000, blocks, 3,
	    false, false, false, &used), 0);
	assert(d[0].control == (32 | AHB_DESC_OWNER));
	assert(d[1].control == (64 | AHB_DESC_OWNER));
	assert(d[2].control == (96 | AHB_DESC_SUC_EOF | AHB_DESC_OWNER));
	CHECK_STATUS(esp_ahb_gdma_desc_build(d, 8, 0x50000000, blocks, 3,
	    true, false, true, &used), EINVAL);
	CHECK_STATUS(esp_ahb_gdma_desc_build(d, 8, 0x50000004, blocks, 3,
	    true, true, true, &used), 0);
	CHECK_STATUS(esp_ahb_gdma_desc_build(d, 8, 0x50000002, blocks, 3,
	    true, true, true, &used), EINVAL);
	CHECK_STATUS(esp_ahb_gdma_desc_build(d, 8, 0x50000000, split, 2,
	    true, true, true, &used), 0);
	assert(used == 4);
	assert((d[0].control & AHB_DESC_SUC_EOF) == 0 &&
	    (d[1].control & AHB_DESC_SUC_EOF) == 0 &&
	    (d[2].control & AHB_DESC_SUC_EOF) != 0 &&
	    (d[3].control & AHB_DESC_SUC_EOF) != 0);
	assert(d[3].next == 0x50000000);
	blocks[1].ds_addr++;
	CHECK_STATUS(esp_ahb_gdma_desc_build(d, 8, 0x50000000, blocks, 3,
	    true, true, true, &used), EINVAL);
	blocks[1].ds_addr--;
	/* EOF, not descriptor DONE, advances period callbacks in a cyclic ring. */
	last = 3;
	CHECK_STATUS(esp_ahb_gdma_advance_eof(&last, eof, 4, 0, true,
	    &periods, &complete), 0);
	assert(last == 0 && periods == 1 && !complete);
	CHECK_STATUS(esp_ahb_gdma_advance_eof(&last, eof, 4, 2, true,
	    &periods, &complete), 0);
	assert(last == 2 && periods == 1 && !complete);
	CHECK_STATUS(esp_ahb_gdma_advance_eof(&last, eof, 4, 3, false,
	    &periods, &complete), 0);
	assert(last == 3 && periods == 1 && complete);
	CHECK_STATUS(esp_ahb_gdma_advance_eof(&last, eof, 4, 1, true,
	    &periods, &complete), EINVAL);
	return 0;
}
