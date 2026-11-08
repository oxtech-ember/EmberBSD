/* SPDX-License-Identifier: BSD-2-Clause */
/* Execute the shared station parsers without firmware or kernel fixtures. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../ieee80211_rsn.h"
#include "../ieee80211_sta.h"

static unsigned checks;
#define CHECK(e) do { \
	checks++; \
	if (!(e)) { \
		fprintf(stderr, "FAIL:%d: %s\n", __LINE__, #e); \
		exit(EXIT_FAILURE); \
	} \
} while (0)

static void
rsn_checks(void)
{
	const uint8_t psk[] = {48, 18, 1, 0, 0, 15, 172, 4,
	    1, 0, 0, 15, 172, 4, 1, 0, 0, 15, 172, 2};
	uint8_t ie[96] = {0};
	size_t n;

	CHECK(ieee80211_sta_rsn_valid(psk, sizeof(psk), false, false));
	CHECK(ieee80211_sta_rsn_valid(psk, sizeof(psk), false, true));
	CHECK(!ieee80211_sta_rsn_valid(psk, sizeof(psk), true, false));
	CHECK(!ieee80211_sta_rsn_valid(NULL, 0, false, false));
	CHECK(!ieee80211_sta_rsn_valid(NULL, sizeof(psk), true, false));
	for (n = 0; n < sizeof(psk); n++)
		CHECK(!ieee80211_sta_rsn_valid(psk, n, false, false));
	memcpy(ie, psk, sizeof(psk));
	ie[1] = 19;
	CHECK(!ieee80211_sta_rsn_valid(ie, 21, false, false));
	ie[1] = 20;
	CHECK(ieee80211_sta_rsn_valid(ie, 22, false, false));
	for (n = 1; n < 4; n++) {
		ie[20] = (uint8_t)(n << 6);
		CHECK(!ieee80211_sta_rsn_valid(ie, 22, false, false));
	}
	ie[20] = 0;
	ie[1] = 21;
	CHECK(!ieee80211_sta_rsn_valid(ie, 23, false, false));
	ie[1] = 22;
	CHECK(ieee80211_sta_rsn_valid(ie, 24, false, false));
	ie[22] = 1;
	CHECK(!ieee80211_sta_rsn_valid(ie, 24, false, false));
	ie[1] = 38;
	CHECK(ieee80211_sta_rsn_valid(ie, 40, false, false));
	for (n = 24; n < 40; n++)
		CHECK(!ieee80211_sta_rsn_valid(ie, n, false, false));
	ie[22] = 255; ie[23] = 255;
	CHECK(!ieee80211_sta_rsn_valid(ie, 40, false, false));
	ie[22] = 1; ie[23] = 0; ie[1] = 42;
	memcpy(ie + 40, "\x00\x0f\xac\x06", 4);
	CHECK(ieee80211_sta_rsn_valid(ie, 44, false, false));
	ie[43] = 11;
	CHECK(!ieee80211_sta_rsn_valid(ie, 44, false, false));
	ie[43] = 6; ie[19] = 8; ie[20] = 0xc0;
	CHECK(ieee80211_sta_rsn_valid(ie, 44, true, false));
	CHECK(!ieee80211_sta_rsn_valid(ie, 44, false, false));
	/* A complete unknown IE can precede RSN; duplicate RSN cannot follow it. */
	memmove(ie + 2, ie, 44); ie[0] = 221; ie[1] = 0;
	CHECK(ieee80211_sta_rsn_valid(ie, 46, true, false));
	CHECK(!ieee80211_sta_rsn_valid(ie, 46, true, true));
	ie[46] = 244; ie[47] = 1; ie[48] = 0x20;
	CHECK(ieee80211_sta_rsn_valid(ie, 49, true, false));
	ie[48] = 0x21;
	CHECK(!ieee80211_sta_rsn_valid(ie, 49, true, false));
	ie[48] = 0x20;
	memcpy(ie + 49, ie + 46, 3);
	CHECK(!ieee80211_sta_rsn_valid(ie, 52, true, false));
	memcpy(ie + 46, psk, sizeof(psk));
	CHECK(!ieee80211_sta_rsn_valid(ie, 66, true, false));
}

static void
action_checks(void)
{
	const unsigned caps = IEEE80211_STA_ACTION_BTM | IEEE80211_STA_ACTION_NR |
	    IEEE80211_STA_ACTION_RM_REPORT | IEEE80211_STA_ACTION_RM_REQUEST;
	const struct {
		uint8_t category, action;
		bool to_ap;
		size_t len;
		unsigned cap;
	} cases[] = {
		{5, 0, false, 5, IEEE80211_STA_ACTION_RM_REQUEST},
		{5, 1, true, 3, IEEE80211_STA_ACTION_RM_REPORT},
		{5, 4, true, 3, IEEE80211_STA_ACTION_NR},
		{5, 5, false, 3, IEEE80211_STA_ACTION_NR},
		{10, 6, true, 4, IEEE80211_STA_ACTION_BTM},
		{10, 7, false, 7, IEEE80211_STA_ACTION_BTM},
		{10, 8, true, 11, IEEE80211_STA_ACTION_BTM}
	};
	uint8_t b[128] = {0};
	size_t i, n;

	CHECK(!ieee80211_sta_action_valid(NULL, 3, true, caps, 1024));
	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		memset(b, 0, sizeof(b));
		b[0] = cases[i].category; b[1] = cases[i].action;
		CHECK(cases[i].len <= sizeof(b) - 3);
		for (n = 0; n < cases[i].len; n++)
			CHECK(!ieee80211_sta_action_valid(b, n, cases[i].to_ap, caps, 1024));
		CHECK(ieee80211_sta_action_valid(b, n, cases[i].to_ap, caps, n));
		CHECK(!ieee80211_sta_action_valid(b, n, !cases[i].to_ap, caps, 1024));
		CHECK(!ieee80211_sta_action_valid(b, n, cases[i].to_ap, caps, n - 1));
		CHECK(!ieee80211_sta_action_valid(b, n, cases[i].to_ap, caps & ~cases[i].cap, 1024));
		/* Unknown elements are bounded but remain available to the supplicant. */
		b[n] = 221; b[n + 1] = 1;
		CHECK(!ieee80211_sta_action_valid(b, n + 2, cases[i].to_ap, caps, 1024));
		CHECK(ieee80211_sta_action_valid(b, n + 3, cases[i].to_ap, caps, 1024));
	}
	memset(b, 0, sizeof(b)); b[0] = 10; b[1] = 7; b[3] = 0x18;
	b[7] = 4; b[8] = 10; b[19] = 3;
	for (n = 7; n < 23; n++)
		CHECK(!ieee80211_sta_action_valid(b, n, false, caps, 1024));
	CHECK(ieee80211_sta_action_valid(b, 23, false, caps, 1024));
	b[8] = 9;
	CHECK(!ieee80211_sta_action_valid(b, 23, false, caps, 1024));
	b[8] = 10; b[7] = 3;
	CHECK(!ieee80211_sta_action_valid(b, 23, false, caps, 1024));
	memset(b, 0, sizeof(b)); b[0] = 5; b[1] = 5; b[3] = 52;
	for (n = 0; n < 13; n++) {
		b[4] = (uint8_t)n;
		CHECK(!ieee80211_sta_action_valid(b, n + 5, false, caps, 1024));
	}
	b[4] = 13;
	CHECK(ieee80211_sta_action_valid(b, 18, false, caps, 1024));
	b[4] = 14;
	CHECK(!ieee80211_sta_action_valid(b, 19, false, caps, 1024));
	b[4] = 16; b[18] = 3; b[19] = 1;
	CHECK(ieee80211_sta_action_valid(b, 21, false, caps, 1024));
	b[19] = 2;
	CHECK(!ieee80211_sta_action_valid(b, 21, false, caps, 1024));
	memset(b, 0, sizeof(b)); b[0] = 5; b[1] = 1; b[3] = 39;
	for (n = 0; n < 3; n++) {
		b[4] = (uint8_t)n;
		CHECK(!ieee80211_sta_action_valid(b, n + 5, true, caps, 1024));
	}
	b[4] = 3;
	CHECK(ieee80211_sta_action_valid(b, 8, true, caps, 1024));
	b[0] = 0;
	CHECK(!ieee80211_sta_action_valid(b, 8, true, caps, 1024));
	CHECK(ieee80211_sta_ies_valid(NULL, 0));
	CHECK(!ieee80211_sta_ies_valid(NULL, 1));
	CHECK(!ieee80211_sta_btm_requested(NULL, 0));
}

int
main(void)
{
	rsn_checks();
	action_checks();
	printf("PASS: %u shared station parser checks\n", checks);
	return EXIT_SUCCESS;
}
