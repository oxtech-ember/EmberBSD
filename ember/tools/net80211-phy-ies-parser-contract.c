/* SPDX-License-Identifier: BSD-2-Clause */
/* Literal wire cases exercise the production bounded PHY IE parser. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ieee80211_phy_ies.h"
static unsigned checks;
#define CHECK(e)                                                               \
	do {                                                                   \
		checks++;                                                      \
		assert(e);                                                     \
	} while (0)
static const uint8_t wire[] = {
    45, 26, 1,	2,  3,	4,  5,	 6,   7,  8,  9,  10, 11, 12, 13,
    14, 15, 16, 17, 18, 19, 20,	 21,  22, 23, 24, 25, 26, 61, 22,
    1,	2,  3,	4,  5,	6,  7,	 8,   9,  10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22,	 191, 12, 1,  2,  3,  4,  5,  6,
    7,	8,  9,	10, 11, 12, 192, 5,   1,  2,  3,  4,  5};
int
main(void)
{
	struct ieee80211_phy_ies ies;
	uint8_t data[512];
	const unsigned starts[] = {0, 28, 52, 66};
	const unsigned lengths[] = {26, 22, 12, 5};
	unsigned i, n;
	CHECK(ieee80211_phy_ies_parse(&ies, wire, sizeof(wire)) == 0);
	CHECK(ies.len == 73 && memcmp(ies.data, wire, 73) == 0);
	CHECK(ieee80211_phy_ies_size(&ies, 73) == 73);
	CHECK(ieee80211_phy_ies_size(&ies, 72) == 0);
	for (i = 0; i < 4; i++) {
		/* A bounded but wrong-length PHY IE invalidates the whole
		 * snapshot. */
		memset(data, 0x37, sizeof(data));
		data[0] = wire[starts[i]];
		for (n = 0; n < 256; n++) {
			data[1] = n;
			CHECK(ieee80211_phy_ies_parse(&ies, data, n + 2) ==
			      (n == lengths[i] ? 0 : -1));
			CHECK(ies.len == (n == lengths[i] ? n + 2 : 0));
		}
		memcpy(data, wire, sizeof(wire));
		memcpy(data + sizeof(wire), wire + starts[i], lengths[i] + 2);
		CHECK(ieee80211_phy_ies_parse(
			  &ies, data, sizeof(wire) + lengths[i] + 2) == -1);
		CHECK(ies.len == 0);
	}
	for (n = 1; n < sizeof(wire); n++) {
		int end = n == 28 || n == 52 || n == 66;
		CHECK(ieee80211_phy_ies_parse(&ies, wire, n) == (end ? 0 : -1));
		CHECK(ies.len == (end ? n : 0));
	}
	data[0] = 3;
	data[1] = 1;
	data[2] = 36;
	memcpy(data + 3, wire, sizeof(wire));
	CHECK(ieee80211_phy_ies_parse(&ies, data, sizeof(wire) + 3) == 0);
	CHECK(ies.len == 73 && memcmp(ies.data, wire, 73) == 0);
	CHECK(ieee80211_phy_ies_parse(&ies, data, 3) == 0 && ies.len == 0);
	CHECK(ieee80211_phy_ies_parse(&ies, NULL, 0) == 0 && ies.len == 0);
	CHECK(ieee80211_phy_ies_parse(&ies, NULL, 1) == -1 && ies.len == 0);
	ies.len = 74;
	CHECK(ieee80211_phy_ies_size(&ies, 255) == 0);
	printf("PHY IE parser: %u checks PASS\n", checks);
	return 0;
}
