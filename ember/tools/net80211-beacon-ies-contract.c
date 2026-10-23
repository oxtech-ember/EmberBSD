/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <net80211/ieee80211_beacon_ies.h>

static unsigned checks;
#define CHECK(x) do { checks++; assert(x); } while (0)
static void
invalid(const uint8_t *p, size_t n)
{
	struct ieee80211_beacon_ies ies, zero = {0};
	memset(&ies, 0xa5, sizeof(ies));
	CHECK(ieee80211_beacon_ies_parse(&ies, p, n) == -1);
	CHECK(memcmp(&ies, &zero, sizeof(ies)) == 0);
}
int
main(void)
{
	struct ieee80211_beacon_ies ies;
	uint8_t ie[257] = {0};
	CHECK(ieee80211_beacon_ies_parse(&ies, NULL, 0) == 0);
	invalid(NULL, 1);
	/* Every body length and truncated boundary, including lone headers. */
	for (unsigned id = 0; id < 256; id++) {
		ie[0] = id;
		for (unsigned length = 0; length < 256; length++) {
			ie[1] = length;
			invalid(ie, 1);
			if (length)
				invalid(ie, length + 1);
		}
	}
	for (unsigned id = 2; id <= 5; id++) {
		if (id == 4)
			continue;
		unsigned min = id == 2 ? 5 : id == 3 ? 1 : 4;
		ie[0] = id;
		for (unsigned n = 0; n < min; n++) {
			ie[1] = n;
			invalid(ie, n + 2);
		}
		ie[1] = min;
		CHECK(ieee80211_beacon_ies_parse(&ies, ie, min + 2) == 0);
	}
	const uint8_t wire[] = {0,0,1,1,2,5,4,7,9,0,0,221,4,0,0x50,0xf2,1};
	CHECK(ieee80211_beacon_ies_parse(&ies, wire, sizeof(wire)) == 0);
	CHECK(ies.tim == wire + 5 && ies.timoff == 5);
	CHECK(ies.security == wire + 11); /* Vendor IE ends exactly at window. */
	CHECK(ies.ssid == wire && ies.rates == wire + 2);
	uint8_t duplicates[] = {0,0,0,1,'x',1,1,2,1,0,127,3,0,0,8,127,0};
	CHECK(ieee80211_beacon_ies_parse(&ies, duplicates, sizeof(duplicates)) == 0);
	CHECK(ies.ssid == duplicates + 2 && ies.rates == duplicates + 8);
	CHECK(ies.extcap == duplicates + 15 && ies.extcap_bounded == duplicates + 10);
	uint8_t wme[] = {221,6,0,0x50,0xf2,2,0,1,221,6,0,0x50,0xf2,2,1,1};
	CHECK(ieee80211_beacon_ies_parse(&ies, wme, sizeof(wme)) == 0);
	CHECK(ies.wme_duplicate == 1 && ies.wme == wme + 8);
	uint8_t phy[28] = {45,26};
	CHECK(ieee80211_beacon_ies_parse(&ies, phy, sizeof(phy)) == 0);
	CHECK(ies.phy.len == sizeof(phy));
	phy[1] = 1;
	CHECK(ieee80211_beacon_ies_parse(&ies, phy, 3) == 0);
	CHECK(ies.phy.len == 0); /* Bad PHY does not reject legacy observations. */
	uint8_t lengths[38] = {0,0,0,33};
	CHECK(ieee80211_beacon_ies_parse(&ies, lengths, 37) == 0);
	CHECK(ies.ssid == lengths + 2 && ies.ssid_valid == lengths);
	printf("Beacon IE parser: %u checks\n", checks);
	return 0;
}
