/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD bounded beacon views and switch-announcement contracts. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <net80211/ieee80211_beacon_ies.h>

static unsigned checks;
#define CHECK(x) do { checks++; assert(x); } while (0)
static void
beacon_intervals(void)
{
	/* Fixed ESS body, interval 100 TU, hidden SSID and one rate. */
	const uint8_t valid[] = {0,0,0,0,0,0,0,0,100,0,1,0,0,0,1,1,12};
	const uint8_t no_ssid[] = {0,0,0,0,0,0,0,0,100,0,1,0,1,1,12};
	const uint8_t no_rates[] = {0,0,0,0,0,0,0,0,100,0,1,0,0,0};
	const uint8_t empty_rates[] = {0,0,0,0,0,0,0,0,100,0,1,0,0,0,1,0};
	const uint8_t long_ssid[50] = {
		[8]=100, [10]=1, [12]=0, [13]=33, [47]=1, [48]=1, [49]=12
	};
	const uint8_t long_rates[32] = {
		[8]=100, [10]=1, [12]=0, [13]=0, [14]=1, [15]=16, [16]=12
	};
	uint8_t body[25] = {0};
	uint8_t ht_body[sizeof(valid) + 24] = {0};
	uint8_t ds_ht_body[sizeof(ht_body) + 3] = {0};

	CHECK(ieee80211_sta_beacon_interval(valid, sizeof(valid), 64) == 100);
	CHECK(ieee80211_sta_beacon_interval(NULL, sizeof(valid), 64) == 0);
	CHECK(ieee80211_sta_beacon_interval(valid, 11, 64) == 0);
	CHECK(ieee80211_sta_beacon_interval(valid, sizeof(valid), 0) == 0);
	CHECK(ieee80211_sta_beacon_interval(valid, sizeof(valid), 256) == 0);
	memcpy(body, valid, sizeof(valid));
	body[10] = 0;
	CHECK(ieee80211_sta_beacon_interval(body, sizeof(valid), 64) == 0);
	body[10] = 3;
	CHECK(ieee80211_sta_beacon_interval(body, sizeof(valid), 64) == 0);
	CHECK(ieee80211_sta_beacon_interval(no_ssid, sizeof(no_ssid), 64) == 0);
	CHECK(ieee80211_sta_beacon_interval(no_rates, sizeof(no_rates), 64) == 0);
	CHECK(ieee80211_sta_beacon_interval(empty_rates, sizeof(empty_rates), 64) == 0);
	CHECK(ieee80211_sta_beacon_interval(long_ssid, sizeof(long_ssid), 64) == 0);
	CHECK(ieee80211_sta_beacon_interval(long_rates, sizeof(long_rates), 64) == 0);

	memcpy(body, valid, sizeof(valid));
	body[17] = 3; body[18] = 1; body[19] = 64;
	CHECK(ieee80211_sta_beacon_interval(body, 20, 64) == 100);
	body[19] = 63;
	CHECK(ieee80211_sta_beacon_interval(body, 20, 64) == 0);
	body[19] = 64; body[18] = 2; body[20] = 0;
	CHECK(ieee80211_sta_beacon_interval(body, 21, 64) == 0);

	/* A 5 GHz beacon can omit DS. Its HT primary must still agree. */
	memcpy(ht_body, valid, sizeof(valid));
	ht_body[17] = 61; ht_body[18] = 22; ht_body[19] = 64;
	CHECK(ieee80211_sta_beacon_interval(ht_body, sizeof(ht_body), 64) == 100);
	ht_body[19] = 60;
	CHECK(ieee80211_sta_beacon_interval(ht_body, sizeof(ht_body), 64) == 0);
	ht_body[19] = 0;
	CHECK(ieee80211_sta_beacon_interval(ht_body, sizeof(ht_body), 64) == 0);
	ht_body[19] = 1;
	CHECK(ieee80211_sta_beacon_interval(ht_body, sizeof(ht_body), 1) == 100);
	CHECK(ieee80211_sta_beacon_interval(ht_body, sizeof(ht_body), 64) == 0);
	/* A matching DS element must not hide a conflicting HT primary. */
	memcpy(ds_ht_body, ht_body, sizeof(ht_body));
	ds_ht_body[sizeof(ht_body)] = 3;
	ds_ht_body[sizeof(ht_body) + 1] = 1;
	ds_ht_body[sizeof(ht_body) + 2] = 64;
	CHECK(ieee80211_sta_beacon_interval(ds_ht_body, sizeof(ds_ht_body), 64) == 0);
	ds_ht_body[19] = 64;
	CHECK(ieee80211_sta_beacon_interval(ds_ht_body, sizeof(ds_ht_body), 64) == 100);

	static const struct {
		size_t len;
		uint8_t data[8];
	} announcements[] = {
		{5,{37,3,1,64,0}}, {6,{60,4,1,118,64,0}},
		{8,{40,6,0,1,10,0,20,0}}
	};
	for (unsigned i = 0; i < sizeof(announcements) / sizeof(announcements[0]); i++) {
		memcpy(body, valid, sizeof(valid));
		memcpy(body + sizeof(valid), announcements[i].data, announcements[i].len);
		CHECK(ieee80211_sta_beacon_interval(body,
		    sizeof(valid) + announcements[i].len, 64) == 0);
	}
	memcpy(body, valid, sizeof(valid));
	body[17] = 127;
	CHECK(ieee80211_sta_beacon_interval(body, 18, 64) == 0);

	/* The caller owns interval policy; decode the wire value unchanged. */
	static const struct {
		uint8_t low, high;
		uint16_t expected;
	} intervals[] = {{0,0,0}, {25,0,25}, {232,3,1000},
	    {24,0,24}, {233,3,1001}};
	for (unsigned i = 0; i < sizeof(intervals) / sizeof(intervals[0]); i++) {
		memcpy(body, valid, sizeof(valid));
		body[8] = intervals[i].low; body[9] = intervals[i].high;
		CHECK(ieee80211_sta_beacon_interval(body, sizeof(valid), 64) ==
		    intervals[i].expected);
	}
}
static void
announcement_bodies(void)
{
	const uint8_t action[][2] = {{0,4},{4,4},{3,0},{0,2},{4,1}};
	uint8_t beacon[20] = {0};
	CHECK(ieee80211_sta_beacon_announcement(beacon, 12) == 0);
	CHECK(ieee80211_sta_beacon_announcement(beacon, 11) == 1);
	CHECK(ieee80211_sta_beacon_announcement(NULL, 0) == 1);
	for (unsigned i = 0; i < sizeof(action) / sizeof(action[0]); i++) {
		CHECK(ieee80211_sta_action_announcement(action[i], 2) == (i < 2));
		CHECK(ieee80211_sta_action_announcement(action[i], 1) == 0);
	}
	CHECK(ieee80211_sta_action_announcement(NULL, 0) == 0);
	CHECK(ieee80211_sta_action_announcement(NULL, 2) == 1);
	beacon[12] = 40; beacon[13] = 6;
	CHECK(ieee80211_sta_beacon_announcement(beacon, sizeof(beacon)) == 1);
	beacon[12] = 127;
	CHECK(ieee80211_sta_beacon_announcement(beacon, sizeof(beacon)) == 0);
	beacon[13] = 7;
	CHECK(ieee80211_sta_beacon_announcement(beacon, sizeof(beacon)) == 1);
}
static void
invalid(const uint8_t *p, size_t n)
{
	struct ieee80211_beacon_ies ies, zero = {0};
	memset(&ies, 0xa5, sizeof(ies));
	CHECK(ieee80211_beacon_ies_parse(&ies, p, n) == -1);
	CHECK(memcmp(&ies, &zero, sizeof(ies)) == 0);
}
static void
vendor_window_ends(void)
{
	struct ieee80211_beacon_ies ies;
	static const uint8_t wpa4[] = {221,4,0,0x50,0xf2,1};
	static const uint8_t wpa5[] = {221,5,0,0x50,0xf2,1,0};
	static const uint8_t wpa6[] = {221,6,0,0x50,0xf2,1,0,1};
	static const uint8_t wme4[] = {221,4,0,0x50,0xf2,2};
	static const uint8_t wme5[] = {221,5,0,0x50,0xf2,2,0};
	static const uint8_t wme6[] = {221,6,0,0x50,0xf2,2,0,1};
	static const struct {
		const uint8_t *data;
		size_t len;
		unsigned security, wme;
	} cases[] = {
		{wpa4, sizeof(wpa4), 1, 0},
		{wpa5, sizeof(wpa5), 1, 0},
		{wpa6, sizeof(wpa6), 1, 0},
		{wme4, sizeof(wme4), 0, 0},
		{wme5, sizeof(wme5), 0, 0},
		{wme6, sizeof(wme6), 0, 1}
	};

	/* Each physical array ends at its declared vendor payload boundary. */
	for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		CHECK(ieee80211_beacon_ies_parse(&ies, cases[i].data,
		    cases[i].len) == 0);
		CHECK(ies.security == (cases[i].security ? cases[i].data : NULL));
		CHECK(ies.wme == (cases[i].wme ? cases[i].data : NULL));
		CHECK(ies.wme_duplicate == 0 && ies.unknown == 0);
	}
	/* A declared six-byte WME body cannot borrow bytes past the window. */
	const uint8_t truncated[] = {221,6,0,0x50,0xf2,2,0};
	invalid(truncated, sizeof(truncated));
}
static void
quiet_announcements(void)
{
	struct ieee80211_beacon_ies ies;
	const uint8_t quiet[] = {40,6,0,1,10,0,20,0};
	const uint8_t duplicate[] = {40,6,0,1,10,0,20,0,40,6,1,0,0,0,0,0};
	CHECK(ieee80211_beacon_ies_parse(&ies, quiet, sizeof(quiet)) == 0);
	CHECK(ies.unknown == 0);
	CHECK(ies.quiet == quiet);
	invalid(duplicate, sizeof(duplicate));
	for (unsigned len = 0; len <= 255; len++) {
		uint8_t ie[257] = {40};
		if (len == 6)
			continue;
		ie[1] = len;
		invalid(ie, len + 2);
	}
}
/* A switch cannot be hidden by a later duplicate or malformed body. */
static void
switch_announcements(void)
{
	struct ieee80211_beacon_ies ies;
	uint8_t csa[] = {37,3,1,64,0};
	uint8_t xcsa[] = {60,4,0,118,64,255};
	uint8_t pair[] = {37,3,1,64,2,60,4,1,118,64,2};
	uint8_t duplicate[] = {37,3,1,64,2,37,3,1,64,1};

	CHECK(ieee80211_beacon_ies_parse(&ies, csa, sizeof(csa)) == 0);
	CHECK(ies.csa == csa && ies.xcsa == NULL && ies.unknown == 0);
	CHECK(ieee80211_beacon_ies_parse(&ies, xcsa, sizeof(xcsa)) == 0);
	CHECK(ies.xcsa == xcsa && ies.csa == NULL && ies.unknown == 0);
	CHECK(ieee80211_beacon_ies_parse(&ies, pair, sizeof(pair)) == 0);
	CHECK(ies.csa == pair && ies.xcsa == pair + 5);
	invalid(duplicate, sizeof(duplicate));
	/* Use complete Extended CSA IEs for its duplicate case. */
	const uint8_t twice[] = {60,4,0,118,64,2,60,4,0,118,64,1};
	invalid(twice, sizeof(twice));
	for (unsigned id = 37; id <= 60; id += 23) {
		uint8_t ie[257] = {0};
		ie[0] = id;
		ie[2] = 1;
		ie[id == 37 ? 3 : 4] = 64;
		for (unsigned len = 0; len <= 255; len++) {
			if (len == (id == 37 ? 3 : 4))
				continue;
			ie[1] = len;
			invalid(ie, len + 2);
		}
	}
	for (unsigned mode = 0; mode <= 255; mode++) {
		csa[2] = xcsa[2] = mode;
		if (mode <= 1) {
			CHECK(ieee80211_beacon_ies_parse(&ies, csa, sizeof(csa)) == 0);
			CHECK(ieee80211_beacon_ies_parse(&ies, xcsa, sizeof(xcsa)) == 0);
		} else {
			invalid(csa, sizeof(csa));
			invalid(xcsa, sizeof(xcsa));
		}
	}
	csa[2] = xcsa[2] = 1;
	csa[3] = xcsa[4] = 0;
	invalid(csa, sizeof(csa));
	invalid(xcsa, sizeof(xcsa));
	/* Operating-class policy belongs to the consumer, not this decoder. */
	xcsa[4] = 64;
	xcsa[3] = 0;
	CHECK(ieee80211_beacon_ies_parse(&ies, xcsa, sizeof(xcsa)) == 0);
}

int
main(void)
{
	beacon_intervals();
	announcement_bodies();
	vendor_window_ends();
	quiet_announcements();
	struct ieee80211_beacon_ies ies;
	switch_announcements();
	uint8_t ie[257] = {0};
	CHECK(ieee80211_beacon_ies_parse(&ies, NULL, 0) == 0);
	invalid(NULL, 1);
	/* Every body length and truncated boundary, including lone headers. */
	for (unsigned id = 0; id < 256; id++) {
		ie[0] = id;
		ie[2] = 1;
		ie[id == 37 ? 3 : 4] = 64;
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
