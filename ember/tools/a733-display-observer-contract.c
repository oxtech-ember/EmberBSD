/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD; execute the actual A733 display snapshot implementation. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef unsigned int u_int;
#define __BIT(n) (UINT32_C(1) << (n))
#include "sun60i_a733_display.h"
#include "display-body.h"

/* Independent offsets from the documented CCU contract, not the driver table. */
static const uint32_t expected_regs[] = {
	0x02e0, 0x05c0, 0x05e0, 0x0a00, 0x0a04, 0x0a74, 0x1604, 0x160c,
	0x1684, 0x168c, 0x1690, 0x16cc, 0x16ec, 0x1928, 0x19a8
};
static unsigned int calls, fail_at, changed_at, checks;
#define CHECK(c) do { const bool result = (c); assert(result); checks++; } while (0)

static int
read_clock(void *arg, uint32_t reg, uint32_t *value)
{
	CHECK(arg == &calls);
	CHECK(calls < 30);
	CHECK(reg == expected_regs[calls % 15]);
	(*(unsigned int *)arg)++;
	if (calls == fail_at)
		return EIO;
	*value = reg ^ UINT32_C(0xa5a5a5a5);
	if (calls == changed_at)
		*value ^= UINT32_C(0x80000001);
	return 0;
}

static int
read_power(void *arg, u_int id, uint32_t *pwpr, uint32_t *pmer, uint32_t *pwsr)
{
	uint32_t *values[] = { pwpr, pmer, pwsr };

	CHECK(arg == &calls);
	CHECK(calls < 4);
	CHECK(id == (calls % 2 == 0 ? 1 : 10));
	(*(unsigned int *)arg)++;
	if (calls == fail_at)
		return ENXIO;
	for (u_int r = 0; r < 3; r++) {
		*values[r] = (id << 16) | (r << 8) | 8;
		if (calls > 2 && changed_at == (calls - 3) * 3 + r + 1)
			*values[r] ^= UINT32_C(0x01000008);
	}
	return 0;
}

int
main(void)
{
	struct sun60i_a733_display_clocks clocks, saved_clocks;
	struct sun60i_a733_display_power power, saved_power;

	memset(&saved_clocks, 0x5a, sizeof(saved_clocks));
	memset(&saved_power, 0x5a, sizeof(saved_power));
	clocks = saved_clocks;
	power = saved_power;
	CHECK(sun60i_a733_display_read_clocks(NULL, &calls, 0x2000, &clocks) == EINVAL);
	CHECK(sun60i_a733_display_read_clocks(read_clock, &calls, 0x2000, NULL) == EINVAL);
	CHECK(sun60i_a733_display_read_power(NULL, &calls, &power) == EINVAL);
	CHECK(sun60i_a733_display_read_power(read_power, &calls, NULL) == EINVAL);
	CHECK(calls == 0 && memcmp(&clocks, &saved_clocks, sizeof(clocks)) == 0);
	CHECK(memcmp(&power, &saved_power, sizeof(power)) == 0);
	for (uint64_t size = 0; size < 0x19ac; size++) {
		CHECK(sun60i_a733_display_read_clocks(read_clock, &calls, size, &clocks) == EINVAL);
		CHECK(calls == 0 && memcmp(&clocks, &saved_clocks, sizeof(clocks)) == 0);
	}
	for (fail_at = 1; fail_at <= 30; fail_at++) {
		calls = 0;
		CHECK(sun60i_a733_display_read_clocks(read_clock, &calls, 0x2000, &clocks) == EIO);
		CHECK(calls == fail_at && memcmp(&clocks, &saved_clocks, sizeof(clocks)) == 0);
	}
	fail_at = 0;
	for (changed_at = 15; changed_at <= 30; changed_at++) {
		calls = 0;
		/* changed_at 15 changes the last first-pass word; all changes are observable. */
		CHECK(sun60i_a733_display_read_clocks(read_clock, &calls, 0x19ac, &clocks) == 0);
		CHECK(calls == 30);
		CHECK(clocks.changed == __BIT(changed_at == 15 ? 14 : changed_at - 16));
	}
	changed_at = 0;
	calls = 0;
	CHECK(sun60i_a733_display_read_clocks(read_clock, &calls, UINT64_MAX, &clocks) == 0);
	CHECK(clocks.changed == 0 && calls == 30);
	for (u_int r = 0; r < 15; r++) {
		CHECK(clocks.sample[0][r] == (expected_regs[r] ^ UINT32_C(0xa5a5a5a5)));
		CHECK(clocks.sample[0][r] == clocks.sample[1][r]);
	}
	for (fail_at = 1; fail_at <= 4; fail_at++) {
		calls = 0;
		CHECK(sun60i_a733_display_read_power(read_power, &calls, &power) == ENXIO);
		CHECK(calls == fail_at && memcmp(&power, &saved_power, sizeof(power)) == 0);
	}
	fail_at = 0;
	for (changed_at = 0; changed_at <= 6; changed_at++) {
		calls = 0;
		CHECK(sun60i_a733_display_read_power(read_power, &calls, &power) == 0);
		CHECK(calls == 4 && power.changed == (changed_at == 0 ? 0 : __BIT(changed_at - 1)));
		for (u_int d = 0; d < 2; d++) {
			for (u_int r = 0; r < 3; r++)
				CHECK(power.sample[0][d][r] == (((d == 0 ? 1 : 10) << 16) | (r << 8) | 8));
		}
	}
	printf("PASS: %u actual display snapshot checks; no display hardware access\n", checks);
	return 0;
}
