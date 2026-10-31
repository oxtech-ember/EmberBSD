/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include "cst9220_frame.h"

int
main(void)
{
	uint8_t report[15] = { 0 };
	struct cst9220_point point;
	assert(cst9220_settle_ticks(1, 100) == 2);
	assert(cst9220_settle_ticks(10, 100) == 2);
	assert(cst9220_settle_ticks(1, 1000) == 2);
	assert(cst9220_settle_ticks(10, 1000) == 11);
	assert(cst9220_settle_ticks(1, 100) == 2);
	assert(cst9220_settle_ticks(10, 100) == 2);
	assert(cst9220_settle_ticks(10, 1000) == 11);

	assert(cst9220_decode(report, sizeof(report), 480, 480, &point) == 0);
	report[0] = 6;
	report[1] = 12;
	report[2] = 23;
	report[3] = 0x45;
	report[5] = 1;
	report[6] = 0xab;
	assert(cst9220_decode(report, sizeof(report), 480, 480, &point) == 1);
	assert(point.x == 196 && point.y == 373);
	report[0] = 0x10; /* release retains the old coordinate */
	assert(cst9220_decode(report, sizeof(report), 480, 480, &point) == 0);
	report[0] = 6;
	report[6] = 0;
	assert(cst9220_decode(report, sizeof(report), 480, 480, &point) < 0);
	assert(cst9220_decode(report, 4, 480, 480, &point) < 0);
	return 0;
}
