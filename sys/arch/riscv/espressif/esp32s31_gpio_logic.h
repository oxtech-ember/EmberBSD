/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _RISCV_ESPRESSIF_ESP32S31_GPIO_LOGIC_H_
#define _RISCV_ESPRESSIF_ESP32S31_GPIO_LOGIC_H_

struct esp32s31_route {
	unsigned int pin;
	unsigned int input;
	unsigned int output;
};

static inline bool
esp32s31_pin_valid(unsigned int pin)
{
	return pin < 62 && pin != 29 && pin != 41;
}

static inline bool
esp32s31_route_decode(uint32_t cell, struct esp32s31_route *route)
{
	struct esp32s31_route decoded = {
		.pin = cell & 0x3f,
		.input = (cell >> 6) & 0x1ff,
		.output = (cell >> 15) & 0x1ff,
	};

	if ((cell >> 24) != 0 || !esp32s31_pin_valid(decoded.pin) ||
	    (decoded.input > 255 && decoded.input != 511) ||
	    (decoded.output > 256 && decoded.output != 511))
		return false;
	*route = decoded;
	return true;
}
#endif
