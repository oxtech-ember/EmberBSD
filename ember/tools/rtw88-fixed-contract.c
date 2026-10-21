/* SPDX-License-Identifier: BSD-2-Clause */
/* Complete native wrapper caller and core; modeled stack layout and fake bus.
 */
#define RTW88_START_CORE_ONLY 1
#define main native_start_main
#include "rtw88-start-contract.c"
#undef main
#undef RTW88_START_CORE_ONLY
#include "fixed-constants.inc"
#include <net80211/_ieee80211.h>
#include <sys/types.h>
struct ieee80211_node;
struct ieee80211com {
	int ic_fixed_rate, ic_state;
	unsigned ic_flags;
	enum ieee80211_opmode ic_opmode;
	enum ieee80211_phymode ic_curmode;
	struct ieee80211_rateset ic_sup_rates[IEEE80211_MODE_MAX];
	struct ieee80211_node *ic_bss;
	struct ieee80211_channel *ic_curchan;
};
struct ieee80211_node {
	struct ieee80211com *ni_ic;
	struct ieee80211_channel *ni_chan;
	struct ieee80211_rateset ni_rates;
	unsigned ni_associd;
	int ni_txrate;
};
#include <dev/ic/rtw88_net_policy.h>
struct rtw88_net {
	struct ieee80211com ic;
	struct rtw88_softc *core;
	struct rtw88_net_policy policy;
	int work_pending;
};
static unsigned
ieee80211_chan2ieee(struct ieee80211com *ic, const struct ieee80211_channel *ch)
{
	(void)ic;
	return ch->ic_freq > 3000 ? (ch->ic_freq - 5000) / 5
				  : (ch->ic_freq - 2407) / 5;
}
static enum ieee80211_phymode
ieee80211_chan2mode(struct ieee80211com *ic, struct ieee80211_channel *ch)
{
	return ch->ic_freq > 3000 ? IEEE80211_MODE_11A : ic->ic_curmode;
}
#include "fixed-caller.inc"
#include "fixed-negotiate.inc"
static const uint8_t legacy[]
    = { 2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108 };
static struct rtw88_net net;
static struct ieee80211_node node;
static struct ieee80211_channel channel;
static uint8_t frame[26];
static void
setup_fixed(struct rtw88_softc *sc)
{
	memset(&net, 0, sizeof(net));
	memset(&node, 0, sizeof(node));
	sc->sc_txq[RTW88_TXQ_BE].rp = sc->sc_txq[RTW88_TXQ_BE].wp;
	net.policy.accepting = 1;
	net.core = sc;
	net.ic.ic_bss = &node;
	node.ni_ic = &net.ic;
	net.ic.ic_opmode = IEEE80211_M_STA;
	net.ic.ic_state = IEEE80211_S_RUN;
	net.ic.ic_curmode = IEEE80211_MODE_11G;
	node.ni_associd = 0xc001;
	channel.ic_freq = 2412;
	node.ni_chan = net.ic.ic_curchan = &channel;
	node.ni_rates.rs_nrates = 12;
	memcpy(node.ni_rates.rs_rates, legacy, 12);
	net.ic.ic_sup_rates[IEEE80211_MODE_11G] = node.ni_rates;
	memset(frame, 0, sizeof(frame));
	frame[0] = 0x08;
	frame[1] = 1;
	frame[4] = frame[10] = frame[16] = 2;
	sc->sc_basic_rates = 0xf;
	sc->sc_channel = 1;
	CHECK(ieee80211_chan2ieee(&net.ic, &channel) == 1);
}
static void
reject(struct rtw88_softc *sc)
{
	unsigned writes = f.writes, syncs = f.syncs;
	unsigned wp = sc->sc_txq[RTW88_TXQ_BE].wp;
	uint64_t encoded = sc->sc_stats.tx_data_encoded;
	CHECK(rtw88_net_tx_submit(&net, &node, RTW88_TXQ_BE, frame, 24) != 0);
	CHECK(f.writes == writes && f.syncs == syncs &&
	    sc->sc_txq[RTW88_TXQ_BE].wp == wp);
	CHECK(sc->sc_stats.tx_data_encoded == encoded);
}
static void
old_bytes(struct rtw88_softc *sc, unsigned q, size_t len)
{
	uint8_t expected[RTW8822C_TX_PACKET_DESC_SIZE];
	unsigned wp = sc->sc_txq[q].wp;
	CHECK(rtw88_tx_raw(sc, q, frame, len) == 0);
	memcpy(expected, sc->sc_txq[q].buf[wp].vaddr, sizeof(expected));
	wp = sc->sc_txq[q].wp;
	CHECK(rtw88_net_tx_submit(&net, &node, q, frame, len) == 0);
	CHECK(memcmp(expected, sc->sc_txq[q].buf[wp].vaddr, sizeof(expected))
	    == 0);
}
int
main(void)
{
	struct rtw88_softc sc;
	int result = native_start_main();
	CHECK(result == 0);
	if (!start_ok(&sc))
		return 1;
	setup_fixed(&sc);
	for (unsigned i = 0; i < 12; i++) {
		net.ic.ic_fixed_rate = (int)i;
		/* Negotiation sorts a reordered peer set: local index is not a
		 * peer index. */
		for (unsigned j = 0; j < 12; j++)
			node.ni_rates.rs_rates[j] = legacy[11 - j] | 0x80;
		CHECK(ieee80211_fix_rate(&node,
			  IEEE80211_R_DOSORT | IEEE80211_R_DOFRATE
			      | IEEE80211_R_DONEGO | IEEE80211_R_DODEL)
		    == 108);
		unsigned wp = sc.sc_txq[RTW88_TXQ_BE].wp;
		CHECK(rtw88_net_tx_submit(&net, &node, RTW88_TXQ_BE, frame, 24)
		    == 0);
		const uint8_t *desc = sc.sc_txq[RTW88_TXQ_BE].buf[wp].vaddr;
		CHECK((desc[16] & 0x7f) == i);
		if (i == 4) /* Local G index4 is 6M, sorted peer index4 is 9M. */
			CHECK((node.ni_rates.rs_rates[4] & IEEE80211_RATE_VAL) == 18);
		CHECK((desc[13] & 5) == 5);
	}
	/* A/B mode indices use their own local tables, never a descriptor
	 * index. */
	for (unsigned mode = 0; mode < 2; mode++) {
		setup_fixed(&sc);
		unsigned first = mode ? 0 : 4, count = mode ? 4 : 8;
		net.ic.ic_curmode
		    = mode ? IEEE80211_MODE_11B : IEEE80211_MODE_11A;
		if (!mode) {
			channel.ic_freq = 5180;
			sc.sc_channel = 36;
		}
		struct ieee80211_rateset *rs
		    = &net.ic.ic_sup_rates[net.ic.ic_curmode];
		rs->rs_nrates = count;
		memcpy(rs->rs_rates, legacy + first, count);
		for (unsigned i = 0; i < count; i++) {
			net.ic.ic_fixed_rate = (int)i;
			unsigned wp = sc.sc_txq[RTW88_TXQ_BE].wp;
			CHECK(rtw88_net_tx_submit(
				  &net, &node, RTW88_TXQ_BE, frame, 24)
			    == 0);
			const uint8_t *d
			    = sc.sc_txq[RTW88_TXQ_BE].buf[wp].vaddr;
			CHECK((d[16] & 0x7f) == first + i);
		}
	}
	setup_fixed(&sc);
	net.ic.ic_fixed_rate = 11;
	net.ic.ic_sup_rates[IEEE80211_MODE_11G].rs_rates[11] = 126;
	reject(&sc);
	setup_fixed(&sc);
	net.ic.ic_fixed_rate = 11;
	frame[0] = 0x88;
	frame[1] |= 0x80;
	reject(&sc);
	setup_fixed(&sc);
	net.ic.ic_fixed_rate = 11;
	frame[0] = 0x88;
	CHECK(rtw88_net_tx_submit(&net, &node, RTW88_TXQ_BE, frame, 26) == 0);
	setup_fixed(&sc);
	net.ic.ic_fixed_rate = 11;
	struct ieee80211_node other = node;
	CHECK(rtw88_net_tx_submit(&net, &other, RTW88_TXQ_BE, frame, 24)
	    == EINVAL);
	struct ieee80211_channel other_channel = channel;
	node.ni_chan = &other_channel;
	reject(&sc);
	setup_fixed(&sc);
	net.ic.ic_fixed_rate = 11;
	net.ic.ic_curmode = IEEE80211_MODE_AUTO;
	reject(&sc);
	setup_fixed(&sc);
	net.ic.ic_fixed_rate = 11;
	net.work_pending = 1;
	reject(&sc);
	net.work_pending = 0;
	net.policy.accepting = 0;
	reject(&sc);
	net.policy.accepting = 1;
	net.policy.dying = 1;
	reject(&sc);
	setup_fixed(&sc);
	net.ic.ic_fixed_rate = 11;
	node.ni_rates.rs_nrates = 1;
	node.ni_rates.rs_rates[0] = 2;
	reject(&sc);
	setup_fixed(&sc);
	net.ic.ic_fixed_rate = -2;
	reject(&sc);
	net.ic.ic_fixed_rate = 12;
	reject(&sc);
	net.ic.ic_fixed_rate = 11;
	net.ic.ic_sup_rates[IEEE80211_MODE_11G].rs_nrates = 255;
	reject(&sc);
	setup_fixed(&sc);
	net.ic.ic_fixed_rate = 11;
	node.ni_rates.rs_nrates = 255;
	reject(&sc);
	setup_fixed(&sc);
	net.ic.ic_fixed_rate = 11;
	node.ni_associd = 0;
	reject(&sc);
	node.ni_associd = 2008;
	reject(&sc);
	setup_fixed(&sc);
	net.ic.ic_fixed_rate = 11;
	net.ic.ic_state = IEEE80211_S_ASSOC;
	reject(&sc);
	setup_fixed(&sc);
	net.ic.ic_fixed_rate = 11;
	net.ic.ic_opmode = IEEE80211_M_HOSTAP;
	reject(&sc);
	setup_fixed(&sc);
	net.ic.ic_fixed_rate = 11;
	sc.sc_channel = 36;
	reject(&sc);
	setup_fixed(&sc);
	net.ic.ic_fixed_rate = 11;
	net.ic.ic_flags = IEEE80211_F_USEPROT;
	reject(&sc);
	/* CCK fixed is permitted with protection; OFDM never silently falls
	 * back. */
	net.ic.ic_fixed_rate = 3;
	CHECK(rtw88_net_tx_submit(&net, &node, RTW88_TXQ_BE, frame, 24) == 0);
	setup_fixed(&sc);
	net.ic.ic_fixed_rate = 11;
	frame[1] = 0;
	reject(&sc);
	frame[1] = 3;
	reject(&sc);
	setup_fixed(&sc);
	net.ic.ic_fixed_rate = 11;
	/* Epoch/rejoin input is read per call, not cached as a former peer
	 * index. */
	node.ni_associd = 0;
	reject(&sc);
	node.ni_associd = 1;
	CHECK(rtw88_net_tx_submit(&net, &node, RTW88_TXQ_BE, frame, 24) == 0);
	setup_fixed(&sc);
	net.ic.ic_fixed_rate = IEEE80211_FIXED_RATE_NONE;
	/* AUTO payload requires a committed firmware RA capsule. */
	CHECK(rtw88_net_tx_submit(&net, &node, RTW88_TXQ_BE, frame, 24) == EBUSY);
	net.ic.ic_fixed_rate
	    = -99; /* Noneligible frames bypass fixed metadata. */
	frame[0] = 0x80;
	old_bytes(&sc, RTW88_TXQ_MGMT, 24);
	frame[0] = 0x48;
	old_bytes(&sc, RTW88_TXQ_BE, 24);
	frame[0] = 0x08;
	frame[16] |= 1;
	old_bytes(&sc, RTW88_TXQ_BE, 24);
	frame[16] &= ~1;
	frame[4] |= 1;
	old_bytes(&sc, RTW88_TXQ_BE, 24);
#ifdef RTW88_FIXED_11G
	/* Corrupt direct metadata is rejected before descriptor publication. */
	setup_fixed(&sc);
	struct rtw88_tx_fixed fixed = { .rate500 = 108,
		.peer_mask = 0xfff,
		.channel = 1,
		.mode = RTW88_FIXED_11G,
		.associated = 1 };
	for (unsigned kind = 0; kind < 9; kind++) {
		struct rtw88_tx_fixed bad = fixed;
		switch (kind) {
		case 0:
			bad.rate500 = 126;
			break;
		case 1:
			bad.peer_mask = 0x8000;
			break;
		case 2:
			bad.peer_mask = 1;
			break;
		case 3:
			bad.associated = 0;
			break;
		case 4:
			bad.channel = 36;
			break;
		case 5:
			bad.mode = RTW88_FIXED_11B;
			break;
		case 6:
			bad.mode = RTW88_FIXED_11A;
			break;
		case 7:
			bad.use_protection = 1;
			break;
		case 8:
			bad.use_protection = 2;
			break;
		}
		unsigned writes = f.writes, syncs = f.syncs;
		unsigned wp = sc.sc_txq[RTW88_TXQ_BE].wp;
		CHECK(rtw88_tx_fixed(&sc, RTW88_TXQ_BE, frame, 24, &bad) != 0);
		CHECK(f.writes == writes && f.syncs == syncs &&
		    sc.sc_txq[RTW88_TXQ_BE].wp == wp);
	}
#endif
	rtw88_core_detach(&sc);
	printf("rtw88 fixed legacy contract: %u assertions, %u failures\n",
	    assertions, failures);
	return failures != 0;
}
