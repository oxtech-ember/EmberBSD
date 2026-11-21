/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "../esp_i2s_state.h"

int
main(void)
{
	uint16_t eof_num = 123;

	/* These are the actual RX/TX CONF words written by the driver. */
	assert((ESP_I2S_RX_CONF_WORD & ESP_I2S_PCM_BYPASS) != 0);
	assert((ESP_I2S_TX_CONF_WORD & ESP_I2S_PCM_BYPASS) != 0);
	assert((ESP_I2S_RX_CONF_WORD & (3U << 10)) == 0);
	assert((ESP_I2S_TX_CONF_WORD & (3U << 10)) == 0);
	/* RX bit 14 is RX_DONE_MODE, not TX's BCK_NO_DLY field. */
	assert((ESP_I2S_RX_CONF_WORD & (1U << 14)) == 0);
	assert((ESP_I2S_TX_CONF_WORD & ESP_I2S_TX_BCK_NO_DLY) != 0);
	assert((ESP_I2S_TX_CONF_WORD & ESP_I2S_TX_MONO_FST_VLD) != 0);
	assert(((ESP_I2S_RX_CONF_WORD >> 21) & 0x3fU) == ESP_I2S_BCK_DIV - 1);
	assert(((ESP_I2S_TX_CONF_WORD >> 21) & 0x3fU) == ESP_I2S_BCK_DIV - 1);
	/* 40 MHz / (3 + 49/192) / 8 / (2 x 16) = 48 kHz. */
	assert((uint64_t)ESP_I2S_XTAL_HZ * ESP_I2S_MCLK_DIV_DEN ==
	    (uint64_t)ESP_I2S_MCLK_HZ *
	    (ESP_I2S_MCLK_DIV_N * ESP_I2S_MCLK_DIV_DEN +
	    ESP_I2S_MCLK_DIV_Z));
	assert(ESP_I2S_MCLK_DIV_X ==
	    ESP_I2S_MCLK_DIV_DEN / ESP_I2S_MCLK_DIV_Z - 1);
	assert(ESP_I2S_MCLK_DIV_Y ==
	    ESP_I2S_MCLK_DIV_DEN % ESP_I2S_MCLK_DIV_Z);
	assert(ESP_I2S_MCLK_HZ == ESP_I2S_SAMPLE_RATE * ESP_I2S_BCK_DIV *
	    ESP_I2S_SLOT_BITS * ESP_I2S_SLOT_COUNT);

	assert(!esp_i2s_rx_eof_encode(0, &eof_num));
	assert(eof_num == 123);
	assert(!esp_i2s_rx_eof_encode(1, NULL));
	assert(esp_i2s_rx_eof_encode(1, &eof_num));
	assert(eof_num == 1);
	assert(esp_i2s_rx_eof_encode(960, &eof_num));
	assert(eof_num == 960); /* S31 field receives DMA bytes, not sample words. */
	assert(esp_i2s_rx_eof_encode(UINT16_MAX, &eof_num));
	assert(eof_num == UINT16_MAX);
	assert(!esp_i2s_rx_eof_encode((size_t)UINT16_MAX + 1, &eof_num));
	assert(!esp_i2s_rx_eof_encode(SIZE_MAX, &eof_num));

	return 0;
}
