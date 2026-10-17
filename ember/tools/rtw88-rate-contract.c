/* SPDX-License-Identifier: BSD-2-Clause */
/* Original native rate negotiation regression checks, AI-assisted. */
#include <sys/types.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <net80211/_ieee80211.h>
#include "rate-constants.inc"

/* Only the kernel object boundary and channel lookup are doubles. */
struct ieee80211_node;
struct ieee80211com {
	int ic_fixed_rate;
	enum ieee80211_opmode ic_opmode;
	struct ieee80211_rateset ic_sup_rates[IEEE80211_MODE_MAX];
	struct ieee80211_node *ic_bss;
};
struct ieee80211_node {
	struct ieee80211com *ni_ic;
	struct ieee80211_channel *ni_chan;
	struct ieee80211_rateset ni_rates;
	int ni_txrate;
};
static enum ieee80211_phymode
ieee80211_chan2mode(struct ieee80211com *ic, struct ieee80211_channel *c)
{
	(void)ic;
	return c->ic_freq > 3000 ? IEEE80211_MODE_11A : IEEE80211_MODE_11G;
}
#include "rate-native.inc"
#include "rate-consumer.inc"

static unsigned checks, failures;
#define CHECK(e) do { checks++; if (!(e)) { failures++; \
	fprintf(stderr, "line %u: %s\n", (unsigned)__LINE__, #e); } } while (0)

static void
setup(struct ieee80211com *ic, struct ieee80211_node *ni,
    struct ieee80211_channel *ch, enum ieee80211_opmode opmode,
    enum ieee80211_phymode mode)
{
	static const struct ieee80211_rateset legacy2 = {
	    12, {2,4,11,22,12,18,24,36,48,72,96,108}
	};
	static const struct ieee80211_rateset legacy5 = {
	    8, {12,18,24,36,48,72,96,108}
	};

	memset(ic, 0, sizeof(*ic));
	memset(ni, 0, sizeof(*ni));
	ch->ic_freq = mode == IEEE80211_MODE_11A ? 5180 : 2437;
	ch->ic_flags = 0;
	ic->ic_fixed_rate = IEEE80211_FIXED_RATE_NONE;
	ic->ic_opmode = opmode;
	ic->ic_bss = ni;
	ic->ic_sup_rates[mode] = mode == IEEE80211_MODE_11A ? legacy5 : legacy2;
	ni->ni_ic = ic;
	ni->ni_chan = ch;
}

int
main(void)
{
	struct ieee80211com ic;
	struct ieee80211_node ni;
	struct ieee80211_channel ch;
	const int negotiate = IEEE80211_R_DOSORT | IEEE80211_R_DOFRATE |
	    IEEE80211_R_DONEGO | IEEE80211_R_DODEL;
	struct ieee80211_rateset before;

	/* AUTO local sets have no BASIC flags. Keep the AP's nondefault set. */
	setup(&ic, &ni, &ch, IEEE80211_M_STA, IEEE80211_MODE_11G);
	ni.ni_rates = (struct ieee80211_rateset){6, {108,0x98,4,0x8c,0x82,48}};
	CHECK(ieee80211_fix_rate(&ni, negotiate) == 108);
	CHECK(rtw88_net_basic_rates(&ic) == 0x51);
	CHECK(ni.ni_rates.rs_nrates == 6 && ni.ni_rates.rs_rates[0] == 0x82);
	CHECK(ni.ni_rates.rs_rates[2] == 0x8c && ni.ni_rates.rs_rates[3] == 0x98);
	before = ni.ni_rates;
	CHECK(ieee80211_fix_rate(&ni, negotiate) == 108);
	CHECK(memcmp(&before, &ni.ni_rates, sizeof(before)) == 0);
	CHECK(rtw88_net_basic_rates(&ic) == 0x51);
	/* A local BASIC flag must not make an AP nonbasic rate basic in STA. */
	ic.ic_sup_rates[IEEE80211_MODE_11G].rs_rates[1] |= IEEE80211_RATE_BASIC;
	ni.ni_rates = (struct ieee80211_rateset){3, {2,4,0x8c}};
	CHECK(ieee80211_fix_rate(&ni, negotiate) == 12);
	CHECK(rtw88_net_basic_rates(&ic) == 0x10);
	CHECK(ni.ni_rates.rs_rates[1] == 4);
	/* 5GHz uses the AP's OFDM mask, with no invented CCK or local defaults. */
	setup(&ic, &ni, &ch, IEEE80211_M_STA, IEEE80211_MODE_11A);
	ni.ni_rates = (struct ieee80211_rateset){4, {108,0x98,0x8c,48}};
	CHECK(ieee80211_fix_rate(&ni, negotiate) == 108);
	CHECK(rtw88_net_basic_rates(&ic) == 0x50);
	CHECK((rtw88_net_basic_rates(&ic) & 0xf) == 0);
	/* Sorting and deletion move intact received bytes and keep zero tail. */
	setup(&ic, &ni, &ch, IEEE80211_M_STA, IEEE80211_MODE_11G);
	ni.ni_rates = (struct ieee80211_rateset){4, {0xfe,0x98,0x82,108}};
	CHECK(ieee80211_fix_rate(&ni, negotiate) == 108);
	CHECK(ni.ni_rates.rs_nrates == 3 && ni.ni_rates.rs_rates[3] == 0);
	CHECK(rtw88_net_basic_rates(&ic) == 0x41);
	CHECK(ni.ni_txrate == 2);
	/* Unsupported-only and missing fixed rates still fail negotiation. */
	ni.ni_rates = (struct ieee80211_rateset){1, {0xfe}};
	CHECK(ieee80211_fix_rate(&ni, negotiate) == 0xfe);
	CHECK(ni.ni_rates.rs_nrates == 0);
	ni.ni_rates = (struct ieee80211_rateset){2, {0x82,0x8c}};
	ic.ic_fixed_rate = 11;
	CHECK((ieee80211_fix_rate(&ni, negotiate) & IEEE80211_RATE_BASIC) != 0);
	ic.ic_fixed_rate = 4;
	CHECK(ieee80211_fix_rate(&ni, negotiate) == 12);
	CHECK(rtw88_net_basic_rates(&ic) == 0x11);
	/* Without DONEGO, AP bytes keep their old behavior. */
	before = ni.ni_rates;
	CHECK(ieee80211_fix_rate(&ni, IEEE80211_R_DOSORT) == 12);
	CHECK(memcmp(&before, &ni.ni_rates, sizeof(before)) == 0);
	/* HOSTAP and IBSS must still publish the local BASIC set. */
	setup(&ic, &ni, &ch, IEEE80211_M_HOSTAP, IEEE80211_MODE_11G);
	ic.ic_sup_rates[IEEE80211_MODE_11G].rs_rates[1] = 0x84;
	ni.ni_rates = (struct ieee80211_rateset){3, {0x82,4,0x8c}};
	CHECK(ieee80211_fix_rate(&ni, negotiate) == 12);
	CHECK(rtw88_net_basic_rates(&ic) == 2);
	CHECK(ni.ni_rates.rs_rates[0] == 2 && ni.ni_rates.rs_rates[2] == 12);
	ni.ni_rates = (struct ieee80211_rateset){2, {0xfe,2}};
	CHECK((ieee80211_fix_rate(&ni, negotiate) & IEEE80211_RATE_BASIC) != 0);
	ic.ic_opmode = IEEE80211_M_IBSS;
	ni.ni_rates = (struct ieee80211_rateset){3, {0x82,4,0x8c}};
	CHECK(ieee80211_fix_rate(&ni, negotiate) == 12);
	CHECK(rtw88_net_basic_rates(&ic) == 2);
	printf("rtw88 native rate negotiation: %u assertions, %u failures\n",
	    checks, failures);
	return failures != 0;
}
