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

#define ESP_I2S_PCM_BYPASS	(1U << 12)
#define ESP_I2S_XTAL_HZ		40000000U
#define ESP_I2S_MCLK_HZ		12288000U
#define ESP_I2S_SAMPLE_RATE	48000U
#define ESP_I2S_MCLK_DIV_N	3U
#define ESP_I2S_MCLK_DIV_X	2U
#define ESP_I2S_MCLK_DIV_Y	45U
#define ESP_I2S_MCLK_DIV_Z	49U
#define ESP_I2S_MCLK_DIV_DEN	192U
#define ESP_I2S_BCK_DIV		8U
#define ESP_I2S_SLOT_BITS	16U
#define ESP_I2S_SLOT_COUNT	2U
#define ESP_I2S_RX_SLAVE_MOD	(1U << 3)
#define ESP_I2S_RX_MONO		(1U << 6)
#define ESP_I2S_RX_MONO_FST_VLD	(1U << 9)
#define ESP_I2S_RX_MSB_SHIFT	(1U << 13)
#define ESP_I2S_RX_TDM_EN	(1U << 19)
#define ESP_I2S_TX_STOP_EN	(1U << 4)
#define ESP_I2S_TX_CHAN_EQUAL	(1U << 5)
#define ESP_I2S_TX_MONO		(1U << 6)
#define ESP_I2S_TX_MONO_FST_VLD	(1U << 9)
#define ESP_I2S_TX_MSB_SHIFT	(1U << 13)
#define ESP_I2S_TX_BCK_NO_DLY	(1U << 14)
#define ESP_I2S_TX_TDM_EN	(1U << 19)
#define ESP_I2S_TX_SIG_LOOPBACK	(1U << 30)
#define ESP_I2S_RX_CONF_WORD	(ESP_I2S_RX_SLAVE_MOD | ESP_I2S_RX_MONO | \
	ESP_I2S_RX_MONO_FST_VLD | ESP_I2S_PCM_BYPASS | \
	ESP_I2S_RX_MSB_SHIFT | ESP_I2S_RX_TDM_EN | \
	((ESP_I2S_BCK_DIV - 1) << 21))
#define ESP_I2S_TX_CONF_WORD	(ESP_I2S_TX_STOP_EN | ESP_I2S_TX_CHAN_EQUAL | \
	ESP_I2S_TX_MONO | ESP_I2S_TX_MONO_FST_VLD | ESP_I2S_PCM_BYPASS | \
	ESP_I2S_TX_MSB_SHIFT | ESP_I2S_TX_BCK_NO_DLY | ESP_I2S_TX_TDM_EN | \
	ESP_I2S_TX_SIG_LOOPBACK | ((ESP_I2S_BCK_DIV - 1) << 21))

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
