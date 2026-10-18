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
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#include "sun60i_a733_display.h"
#include "display-body.h"

/* Independent offsets from the documented CCU contract, not the driver table. */
static const uint32_t expected_regs[] = {
	0x02e0, 0x05c0, 0x05e0, 0x0a00, 0x0a04, 0x0a74, 0x1604, 0x160c,
	0x1684, 0x168c, 0x1690, 0x16cc, 0x16ec, 0x1928, 0x19a8,
	0x02e8, 0x02ec, 0x0580, 0x0a80, 0x0a88, 0x0b00, 0x0c00, 0x1840
};
static unsigned int calls, fail_at, changed_at, checks;
#define CHECK(c) do { const bool result = (c); assert(result); checks++; } while (0)

static int
read_clock(void *arg, uint32_t reg, uint32_t *value)
{
	CHECK(arg == &calls);
	CHECK(calls < 46);
	CHECK(reg == expected_regs[calls % 23]);
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

static void
test_decode(void)
{
	/* Independent table: snapshot slot, last selector, DE3X, DE4X. */
	static const unsigned mux[9][4] = {
		{ 3, 6, 0, 1 }, { 17, 5, 5, 255 }, { 18, 6, 5, 255 },
		{ 19, 6, 5, 255 }, { 20, 6, 6, 255 }, { 21, 4, 3, 255 },
		{ 22, 6, 255, 1 }, { 8, 3, 255, 255 }, { 10, 1, 255, 255 }
	};
	/* Physical ff2ef10 prefix. The eight new census slots are synthetic. */
	static const uint32_t boot[15] = {
		0x4d113100, 0x110103ff, 0xf1055803, 0, 0, 0x10000,
		0x10001, 0, 0, 0x30001, 0x80000000, 0x10001, 0x10000,
		0x30003, 0x30000
	};
	struct sun60i_a733_display_clocks in = { 0 };
	struct sun60i_a733_display_clock_facts out, saved;
	const unsigned before = calls;

	memset(&saved, 0x5a, sizeof(saved));
	out = saved;
	CHECK(sun60i_a733_display_decode_clocks(NULL, &out) == EINVAL);
	CHECK(sun60i_a733_display_decode_clocks(&in, NULL) == EINVAL);
	CHECK(memcmp(&out, &saved, sizeof(out)) == 0);
	for (unsigned i = 0; i < 23; i++) {
		in.sample[1][i] = 1;
		CHECK(sun60i_a733_display_decode_clocks(&in, &out) == EBUSY);
		CHECK(memcmp(&out, &saved, sizeof(out)) == 0);
		in.sample[1][i] = 0;
		in.changed = __BIT(i);
		CHECK(sun60i_a733_display_decode_clocks(&in, &out) == EBUSY);
		CHECK(memcmp(&out, &saved, sizeof(out)) == 0);
		in.changed = 0;
	}
	for (unsigned sample = 0; sample < 2; sample++)
		memcpy(in.sample[sample], boot, sizeof(boot));
	CHECK(sun60i_a733_display_decode_clocks(&in, &out) == 0);
	CHECK(out.pll_control == 0x4c113100);
	CHECK((out.pll_control & 0xb0000000) == 0);
	CHECK(out.pll_gate_control == 0x30003 && out.pll_gate_status == 0x30000);
	CHECK(out.de3x_selected == 1 && out.de4x_selected == 0);
	CHECK(out.gates_enabled == 0x100 && out.bus_gates == 0xa40);
	CHECK(out.resets_deasserted == 0x1860 && out.hdmi_resets == 3);
	CHECK(out.ahb_gates == 0x30 && out.mbus_gates == 0x800);
	CHECK(out.unknown_selectors == 0 && out.update_pending == 0);
	for (unsigned n = 0; n < 9; n++) {
		for (unsigned sel = 0; sel < 8; sel++) {
			for (unsigned gate = 0; gate < 2; gate++) {
				for (unsigned div = 0; div < 32; div++) {
					memset(&in, 0, sizeof(in));
					in.sample[0][mux[n][0]] = (gate << 31) | (sel << 24) | div;
					memcpy(in.sample[1], in.sample[0], sizeof(in.sample[0]));
					CHECK(sun60i_a733_display_decode_clocks(&in, &out) == 0);
					CHECK(out.selector[n] == sel);
					CHECK(out.divisor[n] == (n == 8 ? 1 : div + 1));
					CHECK(out.gates_enabled == (gate << n));
					CHECK(out.unknown_selectors == (sel > mux[n][1] ? __BIT(n) : 0));
					CHECK(((out.de3x_selected >> n) & 1) == (sel == mux[n][2]));
					CHECK(((out.de4x_selected >> n) & 1) == (sel == mux[n][3]));
				}
			}
		}
	}
	for (unsigned div = 0; div < 32; div++) {
		memset(&in, 0, sizeof(in));
		in.sample[0][8] = in.sample[1][8] = div << 8;
		CHECK(sun60i_a733_display_decode_clocks(&in, &out) == 0);
		CHECK(out.hdmi_tv_divisor_n == div + 1);
	}
	for (unsigned n = 0; n < 9; n++) {
		memset(&in, 0, sizeof(in));
		in.sample[0][mux[n][0]] = in.sample[1][mux[n][0]] = 0x18000000;
		CHECK(sun60i_a733_display_decode_clocks(&in, &out) == 0);
		CHECK(out.update_pending == (n == 1 || n == 5 ? __BIT(n) : 0));
		CHECK(out.dynamic_mode == (n == 1 ? __BIT(n) : 0));
	}
	/* Sweep every bit, including reserved bits, in the documented fields. */
	for (unsigned r = 0; r < 23; r++) {
		for (unsigned bit = 0; bit < 32; bit++) {
			memset(&in, 0, sizeof(in));
			in.sample[0][r] = in.sample[1][r] = __BIT(bit);
			CHECK(sun60i_a733_display_decode_clocks(&in, &out) == 0);
			CHECK(out.pll_control == (r == 0 ? __BIT(bit) & 0xfc77ff02 : 0));
			CHECK(out.pll_pattern0 == (r == 15 ? __BIT(bit) & 0x80000000 : 0));
			CHECK(out.pll_pattern1 == (r == 16 ? __BIT(bit) & 0x8f100000 : 0));
			CHECK(out.pll_gate_control == (r == 13 ? __BIT(bit) & 0x30003 : 0));
			CHECK(out.pll_gate_status == (r == 14 ? __BIT(bit) & 0x30000 : 0));
			CHECK(out.bus_gates == (bit == 0 && (r == 4 || r == 6 || r == 7 || r == 9 || r == 11) ? __BIT(r) : 0));
			CHECK(out.resets_deasserted == (bit == 16 && (r == 4 || r == 5 || r == 6 || r == 7 || r == 11 || r == 12) ? __BIT(r) : 0));
			CHECK(out.hdmi_resets == (r == 9 ? (__BIT(bit) >> 16) & 7 : 0));
			CHECK(out.ahb_gates == (r == 1 ? __BIT(bit) & 0x30 : 0));
			CHECK(out.mbus_gates == (r == 2 ? __BIT(bit) & 0x800 : 0));
		}
	}
	CHECK(calls == before);
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
	for (fail_at = 1; fail_at <= 46; fail_at++) {
		calls = 0;
		CHECK(sun60i_a733_display_read_clocks(read_clock, &calls, 0x2000, &clocks) == EIO);
		CHECK(calls == fail_at && memcmp(&clocks, &saved_clocks, sizeof(clocks)) == 0);
	}
	fail_at = 0;
	for (changed_at = 23; changed_at <= 46; changed_at++) {
		calls = 0;
		/* changed_at 23 changes the last first-pass word; all changes are observable. */
		CHECK(sun60i_a733_display_read_clocks(read_clock, &calls, 0x19ac, &clocks) == 0);
		CHECK(calls == 46);
		CHECK(clocks.changed == __BIT(changed_at == 23 ? 22 : changed_at - 24));
	}
	changed_at = 0;
	calls = 0;
	CHECK(sun60i_a733_display_read_clocks(read_clock, &calls, UINT64_MAX, &clocks) == 0);
	CHECK(clocks.changed == 0 && calls == 46);
	for (u_int r = 0; r < 23; r++) {
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
	test_decode();
	printf("PASS: %u actual display snapshot/decode checks; no display hardware access\n", checks);
	return 0;
}
