/* Origin: EmberBSD - validate Goodix report boundaries and button release. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "gt9xx_frame.h"
int
main(void)
{
	uint8_t frame[41] = {0};
	struct gt9xx_contact point;
	assert(gt9xx_decode_frame(frame, 0, 800, 1280, &point) == -1);
	assert(gt9xx_decode_frame(frame, sizeof(frame), 800, 1280, &point) == 0);
	frame[0] = 0x80;
	assert(gt9xx_decode_frame(frame, 1, 800, 1280, &point) == 1);
	assert(!point.pressed);
	frame[0] = 0x81; frame[1] = 2;
	frame[2] = 138; frame[4] = 605 & 255; frame[5] = 605 >> 8;
	assert(gt9xx_decode_frame(frame, 9, 800, 1280, &point) == 1);
	assert(point.pressed && point.id == 2 && point.x == 138 && point.y == 605);
	assert(gt9xx_decode_frame(frame, 8, 800, 1280, &point) == -1);
	frame[2] = 800 & 255; frame[3] = 800 >> 8;
	frame[4] = 1280 & 255; frame[5] = 1280 >> 8;
	assert(gt9xx_decode_frame(frame, 9, 800, 1280, &point) == 1);
	assert(point.x == 800 && point.y == 1280);
	frame[2]++;
	assert(gt9xx_decode_frame(frame, 9, 800, 1280, &point) == -1);
	frame[0] = 0x86;
	assert(gt9xx_decode_frame(frame, sizeof(frame), 800, 1280, &point) == -1);
	memset(frame, 0, sizeof(frame));
	frame[0] = 0x82; frame[1] = 3; frame[2] = 100;
	frame[9] = 1; frame[10] = 200;
	assert(gt9xx_decode_frame(frame, 17, 800, 1280, &point) == 1);
	assert(point.id == 1 && point.x == 200);
	frame[9] = 3;
	assert(gt9xx_decode_frame(frame, 17, 800, 1280, &point) == -1);
	frame[0] = 0x80;
	assert(gt9xx_decode_frame(frame, 1, 800, 1280, &point) == 1);
	assert(!point.pressed);
	puts("Goodix frames: press/move/release, bounds, truncation and contacts passed");
	return 0;
}
