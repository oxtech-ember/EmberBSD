/* SPDX-License-Identifier: BSD-2-Clause */
/* Execute the actual parser with literal wire fixtures and reader failure. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ieee80211_amsdu.h"

static const uint8_t station[6] = { 2, 1, 2, 3, 4, 5 };
static uint8_t wire[4096];
static size_t wirelen;
static int readfail;

static int
read_wire(void *arg, size_t offset, size_t len, void *dst)
{
	(void)arg;
	assert(offset <= wirelen && len <= wirelen - offset);
	if (readfail)
		return -1;
	memcpy(dst, wire + offset, len);
	return 0;
}

static void
fixture(size_t len)
{
	memset(wire, 0, sizeof(wire));
	memcpy(wire, station, 6);
	memcpy(wire + 6, "\x02\x09\x08\x07\x06\x05", 6);
	wire[12] = (uint8_t)(len >> 8);
	wire[13] = (uint8_t)len;
	memcpy(wire + 14, "\xaa\xaa\x03\0\0\0\x08\0", 8);
	wirelen = len + 14;
	readfail = 0;
}

static int
parse(void)
{
	struct ieee80211_amsdu_subframe sub;
	size_t offset = 0;
	int result, count = 0;

	while ((result = ieee80211_amsdu_next(read_wire, NULL, wirelen,
	    station, &offset, &sub)) == 1) {
		assert(sub.offset < wirelen && sub.length >= 8);
		assert(offset <= wirelen);
		count++;
	}
	return result == 0 ? count : -1;
}

int
main(void)
{
	struct ieee80211_amsdu_subframe sub;
	uint8_t valid[22];
	size_t offset, i;
	uint32_t random = 1;

	fixture(8);
	memcpy(valid, wire, 22);
	assert(parse() == 1);
	fixture(1508);
	assert(parse() == 1);
	fixture(1509);
	assert(parse() == -1);
	fixture(7);
	assert(parse() == -1);
	fixture(8);
	wirelen--;
	assert(parse() == -1);
	fixture(8);
	wirelen++;
	assert(parse() == -1); /* A final padding byte is not another subframe. */
	fixture(8);
	wirelen = 46;
	memcpy(wire + 24, valid, 22);
	wire[22] = 0x51; wire[23] = 0xa7;
	assert(parse() == 2); /* Nonzero inter-subframe padding is legal. */
	wire[24 + 13] = 0xff;
	assert(parse() == -1);
	fixture(8); wire[0] = 4; assert(parse() == -1);
	fixture(8); wire[6] = 3; assert(parse() == -1);
	fixture(8); memcpy(wire + 6, station, 6); assert(parse() == -1);
	fixture(8); memset(wire + 6, 0, 6); assert(parse() == -1);
	fixture(8); memcpy(wire, "\xaa\xaa\x03\0\0\0", 6);
	assert(parse() == -1);
	fixture(8); wire[14] = 0; assert(parse() == -1);
	fixture(8); wire[15] = 0; assert(parse() == -1);
	fixture(8); wire[16] = 0; assert(parse() == -1);
	fixture(8); wire[17] = 1; assert(parse() == -1);
	fixture(8); wire[18] = 1; assert(parse() == -1);
	fixture(8); wire[19] = 1; assert(parse() == -1);
	fixture(8); wire[19] = 0xf8; assert(parse() == 1);
	fixture(8); wire[20] = 5; assert(parse() == -1);
	fixture(8); wire[20] = 0x88; wire[21] = 0x8e;
	assert(parse() == -1);
	fixture(8); readfail = 1; assert(parse() == -1);
	fixture(8); offset = 23;
	assert(ieee80211_amsdu_next(read_wire, NULL, wirelen, station,
	    &offset, &sub) == -1 && offset == 23);
	/* Literal 3839-byte aggregate: 1512 + 1512 + 815 bytes. */
	fixture(1498); memcpy(wire + 1512, wire, 1512);
	memcpy(wire + 3024, valid, 22);
	wire[3024 + 12] = 3; wire[3024 + 13] = 0x21;
	wirelen = 3839;
	assert(parse() == 3);
	wirelen = 3840;
	assert(parse() == -1);
	/* Bounded randomized malformed input executes the real checked reader. */
	for (i = 0; i < 50000; i++) {
		size_t j;
		random = random * 1664525U + 1013904223U;
		wirelen = random % sizeof(wire);
		for (j = 0; j < wirelen; j++) {
			random = random * 1664525U + 1013904223U;
			wire[j] = (uint8_t)(random >> 24);
		}
		(void)parse();
	}
	puts("A-MSDU parser: boundary/policy cases + 50000 malformed inputs PASS");
	return 0;
}
