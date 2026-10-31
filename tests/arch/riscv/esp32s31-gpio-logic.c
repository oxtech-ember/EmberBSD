/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "esp32s31_gpio_logic.h"

#define CELL(pin, in, out) ((pin) | ((in) << 6) | ((out) << 15))

int
main(void)
{
	struct esp32s31_route r;

	assert(esp32s31_pin_valid(0));
	assert(esp32s31_pin_valid(61));
	assert(!esp32s31_pin_valid(29));
	assert(!esp32s31_pin_valid(41));
	assert(!esp32s31_pin_valid(62));
	assert(esp32s31_route_decode(CELL(51, 54, 54), &r));
	assert(r.pin == 51 && r.input == 54 && r.output == 54);
	assert(esp32s31_route_decode(CELL(56, 511, 256), &r));
	assert(!esp32s31_route_decode(CELL(29, 0, 0), &r));
	assert(!esp32s31_route_decode(CELL(0, 256, 0), &r));
	assert(!esp32s31_route_decode(CELL(0, 0, 257), &r));
	assert(!esp32s31_route_decode(CELL(0, 0, 0) | (1u << 24), &r));
	return 0;
}
