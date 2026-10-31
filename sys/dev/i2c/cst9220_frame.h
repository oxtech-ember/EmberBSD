/*
 * Copyright (c) 2026 The EmberBSD Project.
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: BSD-2-Clause AND Apache-2.0
 *
 * CST9220 report format based on esp_lcd_touch_cst9220 v0.1.1:
 * espressif/esp-iot-solution 8eee17c20da66048faa20c683ffeb80bfa85d723,
 * components/display/lcd_touch/esp_lcd_touch_cst9220/esp_lcd_touch_cst9220.c.
 * Apache-2.0 terms are in LICENSE.cst9220.
 */
#ifndef _DEV_I2C_CST9220_FRAME_H_
#define _DEV_I2C_CST9220_FRAME_H_

struct cst9220_point {
	uint16_t x, y;
};

/* The first tick may be partial; reserve one full extra tick. */
static inline int
cst9220_settle_ticks(unsigned int ms, unsigned int tick_rate)
{
	uint64_t ticks = ((uint64_t)ms * tick_rate + 999) / 1000;

	return ticks >= INT_MAX ? INT_MAX : (int)ticks + 1;
}

/* Return one for a press, zero for release, or -1 for invalid data. */
static inline int
cst9220_decode(const uint8_t *data, size_t len, unsigned int width,
               unsigned int height, struct cst9220_point *point)
{
	unsigned int count, i;

	if (len < 15 || width == 0 || height == 0)
		return -1;
	if (data[0] == 0)
		return 0;
	if (data[0] == 0xab || data[6] != 0xab)
		return -1;
	count = data[5] & 0x7f;
	if (count > 2)
		count = 2;
	for (i = 0; i < count; i++) {
		const uint8_t *p = data + (i ? 7 : 0);
		unsigned int x, y;

		if ((p[0] & 0x0f) >> 1 != 3)
			continue;
		x = (p[1] << 4) | (p[3] >> 4);
		y = (p[2] << 4) | (p[3] & 15);
		if (x >= width || y >= height)
			return -1;
		point->x = x;
		point->y = y;
		return 1;
	}
	return 0;
}
#endif
