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
