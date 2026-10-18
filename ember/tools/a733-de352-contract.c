/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted. Independent expected hardware byte vectors. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef unsigned int u_int;
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#include "sun60i_a733_de352.h"
#include "sun60i_a733_de352.c"

static unsigned checks;
#define CHECK(c) do { const bool ok = (c); assert(ok); checks++; } while (0)

static void
reject_rgb(struct sun60i_a733_de352_rgb in, int expected)
{
	struct sun60i_a733_de352_rgb_words out, saved;

	memset(&out, 0xa5, sizeof(out));
	saved = out;
	CHECK(sun60i_a733_de352_rgb_encode(&in, &out) == expected);
	CHECK(memcmp(&out, &saved, sizeof(out)) == 0);
}

static void
rgb(void)
{
	struct sun60i_a733_de352_rgb in = { 640, 480, 2560, 0x1234567800, 1228800 };
	struct sun60i_a733_de352_rgb_words out;

	CHECK(sun60i_a733_de352_rgb_encode(&in, &out) == 0);
	CHECK(out.attribute == 0xff000403 && out.size == 0x01df027f);
	CHECK(out.pitch == 2560 && out.address_low == 0x34567800);
	CHECK(out.address_high_byte == 0x12 && out.required_bytes == 1228800);
	CHECK(out.coordinate == 0 && out.channel_size == out.size);
	CHECK(out.bottom_low == 0 && out.bottom_high_byte == 0 && out.fill_color == 0);
	CHECK(out.coarse_horizontal == 0 && out.coarse_vertical == 0);
	in = (struct sun60i_a733_de352_rgb){ 1, 1, 4, 0, 4 };
	CHECK(sun60i_a733_de352_rgb_encode(&in, &out) == 0);
	CHECK(out.size == 0 && out.required_bytes == 4);
	/* The last padded row is owned too; a visible-pixel-only span is rejected. */
	in = (struct sun60i_a733_de352_rgb){ 3, 2, 32, 0, 64 };
	CHECK(sun60i_a733_de352_rgb_encode(&in, &out) == 0);
	CHECK(out.required_bytes == 64);
	in.mapped_bytes = 44;
	reject_rgb(in, ERANGE);
	in = (struct sun60i_a733_de352_rgb){ 8192, 8192, 32768, 0xfff0000000, 268435456 };
	CHECK(sun60i_a733_de352_rgb_encode(&in, &out) == 0);
	CHECK(out.size == 0x1fff1fff && out.address_high_byte == 255);
	CHECK(out.address_low == 0xf0000000 && out.required_bytes == 268435456);
	in.dma += 4;
	reject_rgb(in, ERANGE);
	static const struct sun60i_a733_de352_rgb bad[] = {
		{ 0, 1, 4, 0, 4 }, { 1, 0, 4, 0, 4 },
		{ 8193, 1, 32772, 0, 32772 }, { 1, 8193, 4, 0, 32772 },
		{ UINT32_MAX, 1, UINT32_MAX, 0, UINT64_MAX },
		{ 1, UINT32_MAX, 4, 0, UINT64_MAX },
		{ 1, 1, 0, 0, 4 }, { 2, 1, 4, 0, 4 }, { 1, 1, 5, 0, 5 },
		{ 1, 1, 4, 1, 4 }, { 1, 1, 4, 2, 4 }, { 1, 1, 4, 3, 4 }
	};
	for (unsigned i = 0; i < __arraycount(bad); i++)
		reject_rgb(bad[i], EINVAL);
	static const struct sun60i_a733_de352_rgb range[] = {
		{ 1, 1, 4, 0, 3 }, { 1, 1, 4, 0x10000000000, 4 },
		{ 1, 1, 4, 0xfffffffffffffffc, UINT64_MAX },
		{ 1, 8192, 0xfffffffc, 0, UINT64_MAX }
	};
	for (unsigned i = 0; i < __arraycount(range); i++)
		reject_rgb(range[i], ERANGE);
	CHECK(sun60i_a733_de352_rgb_encode(NULL, &out) == EINVAL);
	CHECK(sun60i_a733_de352_rgb_encode(&in, NULL) == EINVAL);
}

static void
reject_rcq(struct sun60i_a733_de352_rcq_block in, int expected)
{
	uint8_t out[16], saved[16];

	memset(out, 0xa5, sizeof(out));
	memcpy(saved, out, sizeof(saved));
	CHECK(sun60i_a733_de352_rcq_encode(&in, out) == expected);
	CHECK(memcmp(out, saved, sizeof(out)) == 0);
}

static void
rcq(void)
{
	struct sun60i_a733_de352_rcq_block in = { 0xab12345660, 0x1c, 0x1c10e0, true };
	uint8_t out[16];
	static const uint8_t expected[16] = {
		0x60, 0x56, 0x34, 0x12, 0x1c, 0, 0, 0xab,
		1, 0, 0, 0, 0xe0, 0x10, 0x1c, 0
	};

	CHECK(sun60i_a733_de352_rcq_encode(&in, out) == 0);
	CHECK(memcmp(out, expected, sizeof(out)) == 0);
	in.dirty = false;
	CHECK(sun60i_a733_de352_rcq_encode(&in, out) == 0);
	CHECK(out[8] == 0 && memcmp(out, expected, 8) == 0 &&
	    memcmp(out + 9, expected + 9, 7) == 0);
	in = (struct sun60i_a733_de352_rcq_block){ 0xffffffffe0, 32, 0x3fffe0, true };
	CHECK(sun60i_a733_de352_rcq_encode(&in, out) == 0);
	CHECK(out[0] == 0xe0 && out[3] == 0xff && out[7] == 0xff);
	static const struct sun60i_a733_de352_rcq_block bad[] = {
		{ 0, 0, 0, true }, { 0, 0x1000000, 0, true },
		{ 0, 1, 0, true }, { 0, 2, 0, true }, { 0, 3, 0, true },
		{ 0, 4, 1, true }, { 0, 4, 2, true }, { 0, 4, 3, true }
	};
	for (unsigned i = 0; i < __arraycount(bad); i++)
		reject_rcq(bad[i], EINVAL);
	for (unsigned i = 1; i < 32; i++)
		reject_rcq((struct sun60i_a733_de352_rcq_block){ i, 4, 0, true }, EINVAL);
	static const struct sun60i_a733_de352_rcq_block range[] = {
		{ 0x10000000000, 4, 0, true }, { 0xffffffffffffffe0, 4, 0, true },
		{ 0xffffffffe0, 36, 0, true }, { 0, 4, 0x400000, true },
		{ 0, 4, 0xfffffffc, true }, { 0, 36, 0x3fffe0, true }
	};
	for (unsigned i = 0; i < __arraycount(range); i++)
		reject_rcq(range[i], ERANGE);
	CHECK(sun60i_a733_de352_rcq_encode(NULL, out) == EINVAL);
	CHECK(sun60i_a733_de352_rcq_encode(&in, NULL) == EINVAL);
}

static void
list_size(void)
{
	uint32_t out;

	for (uint32_t i = 1; i <= 16; i++) {
		CHECK(sun60i_a733_de352_rcq_list_size(i, &out) == 0);
		CHECK(out == ((i + 1) / 2) * 32);
	}
	CHECK(sun60i_a733_de352_rcq_list_size(268435454, &out) == 0);
	CHECK(out == 4294967264U);
	out = 0xa5a5a5a5;
	CHECK(sun60i_a733_de352_rcq_list_size(0, &out) == EINVAL);
	CHECK(out == 0xa5a5a5a5);
	CHECK(sun60i_a733_de352_rcq_list_size(268435455, &out) == EOVERFLOW);
	CHECK(out == 0xa5a5a5a5);
	CHECK(sun60i_a733_de352_rcq_list_size(UINT32_MAX, &out) == EOVERFLOW);
	CHECK(out == 0xa5a5a5a5);
	CHECK(sun60i_a733_de352_rcq_list_size(1, NULL) == EINVAL);
}

int
main(void)
{
	rgb();
	rcq();
	list_size();
	printf("PASS: DE3.52 RGB/RCQ format: %u checks; no hardware access\n", checks);
	return 0;
}
