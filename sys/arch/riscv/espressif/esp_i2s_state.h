/* $NetBSD$ */
/* SPDX-FileCopyrightText: 2026 The EmberBSD Project */
/* SPDX-License-Identifier: BSD-2-Clause */

#ifndef _RISCV_ESPRESSIF_ESP_I2S_STATE_H_
#define _RISCV_ESPRESSIF_ESP_I2S_STATE_H_

#ifndef _KERNEL
#include <stdbool.h>
#include <stdint.h>
#endif
#include <sys/types.h>

static inline size_t
esp_i2s_dma_alignment(size_t cache_align)
{
	return cache_align > 64 ? cache_align : 64;
}

static inline bool
esp_i2s_halt_required(bool running, bool buffer_active, bool broken)
{
	return running || buffer_active || broken;
}

static inline bool
esp_i2s_stop_tx_clock(bool playback_active, bool capture_active)
{
	return !playback_active && !capture_active;
}

/* S31 RXEOF_NUM is programmed in DMA buffer bytes, as in ESP-IDF. */
static inline bool
esp_i2s_rx_eof_encode(size_t period_bytes, uint16_t *eof_num)
{
	if (eof_num == NULL || period_bytes == 0 || period_bytes > 0xffffU)
		return false;
	*eof_num = (uint16_t)period_bytes;
	return true;
}

#endif
