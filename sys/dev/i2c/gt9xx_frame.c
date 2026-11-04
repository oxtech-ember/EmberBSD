/* Origin: EmberBSD - decode Goodix GT9xx reports without touching hardware. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include "gt9xx_frame.h"

size_t
gt9xx_report_size(unsigned int count, unsigned int limit)
{
	if (limit == 0 || limit > 10 || count > limit)
		return (0);
	return (1 + count * 8);
}

/* Register layout: Linux goodix.c, goodix_read_config(), GT9110 variant. */
int
gt9110_decode_config(const uint8_t *cfg, size_t len, uint16_t *maxx,
    uint16_t *maxy, unsigned int *contacts)
{
	unsigned int x, y, n;

	if (len < 7)
		return (-1);
	x = cfg[1] | cfg[2] << 8;
	y = cfg[3] | cfg[4] << 8;
	n = cfg[5] & 15;
	if (x < 2 || x > 8192 || y < 2 || y > 8192 || n == 0 || n > 10)
		return (-1);
	*maxx = x - 1;
	*maxy = y - 1;
	*contacts = n;
	return (0);
}

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
	if (count > 10 || size < 1 + count * 8)
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
