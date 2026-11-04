/* Origin: EmberBSD - validate Goodix report boundaries and button release. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "gt9xx_frame.h"
int
main(void)
{
	uint8_t frame[81] = {0};
	struct gt9xx_contact point;
	uint8_t cfg[7] = { 0x41, 0x00, 0x0f, 0x8e, 0x08, 0x0a, 0x3d };
	uint16_t x, y;
	unsigned int contacts;

	assert(gt9xx_report_size(0, 5) == 1);
	assert(gt9xx_report_size(5, 5) == 41);
	assert(gt9xx_report_size(6, 5) == 0);
	assert(gt9xx_report_size(10, 10) == 81);
	assert(gt9xx_report_size(11, 10) == 0);
	assert(gt9xx_report_size(1, 0) == 0);
	assert(gt9xx_report_size(1, 11) == 0);
	assert(gt9110_decode_config(cfg, 7, &x, &y, &contacts) == 0 &&
	    x == 3839 && y == 2189 && contacts == 10);
	assert(gt9110_decode_config(cfg, 6, &x, &y, &contacts) == -1);
	cfg[5] = 11;
	assert(gt9110_decode_config(cfg, 7, &x, &y, &contacts) == -1);
	cfg[5] = 0;
	assert(gt9110_decode_config(cfg, 7, &x, &y, &contacts) == -1);
	cfg[5] = 10;
	cfg[1] = 0;
	cfg[2] = 0;
	assert(gt9110_decode_config(cfg, 7, &x, &y, &contacts) == -1);
	cfg[1] = 1;
	assert(gt9110_decode_config(cfg, 7, &x, &y, &contacts) == -1);
	cfg[1] = 0;
	cfg[2] = 32;
	assert(gt9110_decode_config(cfg, 7, &x, &y, &contacts) == 0 &&
	    x == 8191);
	cfg[1] = 1;
	assert(gt9110_decode_config(cfg, 7, &x, &y, &contacts) == -1);
	cfg[1] = 0;
	cfg[2] = 15;
	cfg[3] = 0;
	cfg[4] = 0;
	assert(gt9110_decode_config(cfg, 7, &x, &y, &contacts) == -1);
	cfg[3] = 1;
	assert(gt9110_decode_config(cfg, 7, &x, &y, &contacts) == -1);
	cfg[3] = 0;
	cfg[4] = 32;
	assert(gt9110_decode_config(cfg, 7, &x, &y, &contacts) == 0 &&
	    y == 8191);
	cfg[3] = 1;
	assert(gt9110_decode_config(cfg, 7, &x, &y, &contacts) == -1);
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
	memset(frame, 0, sizeof(frame));
	frame[0] = 0x8a;
	for (unsigned int i = 0; i < 10; i++)
		frame[1 + 8 * i] = i;
	assert(gt9xx_decode_frame(frame, 81, 3840, 2190, &point) == 1);
	assert(gt9xx_decode_frame(frame, 80, 3840, 2190, &point) == -1);
	frame[73] = 0;
	assert(gt9xx_decode_frame(frame, 81, 3840, 2190, &point) == -1);
	frame[73] = 9;
	frame[0] = 0x8b;
	assert(gt9xx_decode_frame(frame, 81, 3840, 2190, &point) == -1);
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
