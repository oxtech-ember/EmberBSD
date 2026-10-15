/* Origin: EmberBSD - decode Goodix GT9xx reports without touching hardware. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include "gt9xx_frame.h"

int
gt9xx_decode_frame(const uint8_t *data, size_t size, uint16_t maxx,
    uint16_t maxy, struct gt9xx_contact *point)
{
	uint16_t seen = 0;
	unsigned int count;

	if (size < 1 || maxx == 0 || maxy == 0)
		return -1;
	if ((data[0] & 0x80) == 0)
		return 0;
	count = data[0] & 15;
	if (count > 5 || size < 1 + count * 8)
		return -1;
	point->pressed = false;
	point->id = 0;
	point->x = point->y = 0;
	for (unsigned int i = 0; i < count; i++) {
		const uint8_t *p = data + 1 + i * 8;
		const unsigned int id = p[0];
		const uint16_t x = p[1] | p[2] << 8;
		const uint16_t y = p[3] | p[4] << 8;

		if (id > 15 || (seen & (1U << id)) != 0 ||
		    x > maxx || y > maxy)
			return -1;
		seen |= 1U << id;
		/* A single wscons pointer follows the lowest reported contact ID. */
		if (!point->pressed || id < point->id) {
			point->pressed = true;
			point->id = id;
			point->x = x;
			point->y = y;
		}
	}
	return 1;
}
