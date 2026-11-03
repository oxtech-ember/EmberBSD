/* $NetBSD$ */
/* SPDX-FileCopyrightText: 2026 The EmberBSD Project */
/* SPDX-License-Identifier: BSD-2-Clause */

#ifndef _RISCV_ESPRESSIF_ESP_I2S_STATE_H_
#define _RISCV_ESPRESSIF_ESP_I2S_STATE_H_

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

#endif
