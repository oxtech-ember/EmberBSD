/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Original bounded adaptation of wire semantics in FreeBSD net80211
 * d2145e9b96448da34d3cfede24c509af6d830c92 ieee80211.h/ieee80211_ht.c,
 * BSD-2-Clause, Copyright (c) 2007-2008 Sam Leffler, Errno Consulting.
 * No vap/LinuxKPI ABI or automatic driver PHY admission is imported. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#include <net/if.h>
#include <net/if_media.h>
#include <net80211/ieee80211_var.h>

int
ieee80211_ht20_optional(const void *data, size_t len)
{
	const uint8_t *p = data;
	size_t size;

	if (p == NULL)
		return 0;
	while (len != 0) {
		if (p[0] == 45 || p[0] == 61 || p[0] == 191 || p[0] == 192)
			return EINVAL;
		/* Existing opaque RSN/WME optional-byte handling is unchanged. */
		if (len < 2 || p[1] > len - 2)
			return 0;
		size = (size_t)p[1] + 2;
		/* Legacy Broadcom HT vendor IEs. WME/RSN are unrelated. */
		if (p[0] == 221 && p[1] >= 4 && p[2] == 0 && p[3] == 0x90 &&
		    p[4] == 0x4c && (p[5] == 0x33 || p[5] == 0x34))
			return EINVAL;
		p += size;
		len -= size;
	}
	return 0;
}

static int
ht20_local(struct ieee80211com *ic, struct ieee80211_ht20_profile *p)
{

	memset(p, 0, sizeof(*p));
	return ieee80211_rxba_ready(ic) &&
	    ic->ic_ht20_admit != NULL && ic->ic_ht20_admit(ic, p) == 0 &&
	    p->generation != 0 && p->ready == IEEE80211_HT20_READY_ALL &&
	    (p->ampdu & 0xe3) == 0 &&
	    (p->rx_mcs & 0xff) == 0xff && p->rx_mcs == p->tx_mcs;
}

int
ieee80211_ht20_prepare(struct ieee80211com *ic, struct ieee80211_node *ni,
    int wme, struct ieee80211_ht20_profile *p)
{

	if (!wme || ic->ic_wme.wme_sta_apply == NULL || ic->ic_opmode != IEEE80211_M_STA ||
	    ic->ic_state != IEEE80211_S_ASSOC || ni != ic->ic_bss ||
	    ni->ni_chan != ic->ic_curchan ||
	    ((ic->ic_flags & IEEE80211_F_PRIVACY) != 0 &&
	     ni->ni_rsn.rsn_ucastcipher != IEEE80211_CIPHER_AES_CCM))
		return 0;
	return ht20_local(ic, p);
}

void
ieee80211_ht20_write(uint8_t *p, const struct ieee80211_ht20_profile *profile)
{

	memset(p, 0, 28);
	p[0] = 45;
	p[1] = 26;
	p[2] = 0x0c; /* SMPS disabled, not static; mixed format/20MHz/long GI. */
	p[4] = profile->ampdu; /* Minimum RX A-MPDU8191, qualified spacing. */
	p[5] = profile->rx_mcs;
	p[6] = profile->rx_mcs >> 8;
	p[17] = 1; /* MCS-set byte12: TX defined, equal to RX (validated). */
}

void
ieee80211_ht20_reset(struct ieee80211_node *ni)
{
	uint32_t next = ni->ni_ht20.attempt + 1;

	ieee80211_rxba_reset(ni->ni_ic, ni);
	ni->ni_flags &= ~IEEE80211_NODE_AMSDU_RX;
	memset(&ni->ni_ht20, 0, sizeof(ni->ni_ht20));
	ni->ni_ht20.attempt = next != 0 ? next : 1;
}

void
ieee80211_ht20_offer(struct ieee80211com *ic, struct ieee80211_node *ni,
    int type, const struct ieee80211_ht20_profile *profile)
{
	struct ieee80211_ht20_node *h = &ni->ni_ht20;

	if (profile == NULL || ic->ic_opmode != IEEE80211_M_STA ||
	    ic->ic_state != IEEE80211_S_ASSOC || ni != ic->ic_bss ||
	    ni->ni_chan != ic->ic_curchan)
		return;
	h->profile = *profile;
	h->channel = ic->ic_curchan;
	IEEE80211_ADDR_COPY(h->bssid, ni->ni_bssid);
	h->subtype = type;
	h->offer_attempt = h->attempt;
	h->security = (ic->ic_flags & IEEE80211_F_PRIVACY) != 0;
	h->offered = 1;
}

static int
ht20_current(struct ieee80211com *ic, struct ieee80211_node *ni,
    const struct ieee80211_ht20_node *h)
{
	struct ieee80211_ht20_profile local;

	return ic->ic_opmode == IEEE80211_M_STA &&
	    (ic->ic_state == IEEE80211_S_ASSOC || ic->ic_state == IEEE80211_S_RUN) &&
	    (ic->ic_flags & IEEE80211_F_WME) && (ic->ic_caps & IEEE80211_C_WME) &&
	    ic->ic_wme.wme_sta_apply != NULL &&
	    (ni->ni_flags & (IEEE80211_NODE_QOS | IEEE80211_NODE_WME_VALID)) ==
	    (IEEE80211_NODE_QOS | IEEE80211_NODE_WME_VALID) &&
	    h->security == ((ic->ic_flags & IEEE80211_F_PRIVACY) != 0) &&
	    ((ic->ic_flags & IEEE80211_F_PRIVACY) == 0 ||
	     ni->ni_rsn.rsn_ucastcipher == IEEE80211_CIPHER_AES_CCM) &&
	    ni == ic->ic_bss && h->offer_attempt == h->attempt &&
	    h->channel == ic->ic_curchan && ni->ni_chan == ic->ic_curchan &&
	    IEEE80211_ADDR_EQ(h->bssid, ni->ni_bssid) && ht20_local(ic, &local) &&
	    local.generation == h->profile.generation &&
	    local.rx_mcs == h->profile.rx_mcs && local.tx_mcs == h->profile.tx_mcs &&
	    local.ampdu == h->profile.ampdu;
}

/* Full TLV preflight before the legacy association parser can return early. */
int
ieee80211_ht20_check_ies(const uint8_t *p, size_t len)
{
	unsigned seen = 0, bit;
	size_t size;

	while (len != 0) {
		if (len < 2 || p[1] > len - 2)
			return EINVAL;
		size = (size_t)p[1] + 2;
		bit = p[0] == 45 ? 1 : p[0] == 61 ? 2 : 0;
		if (bit != 0) {
			if ((seen & bit) || p[1] != (bit == 1 ? 26 : 22))
				return EINVAL;
			seen |= bit;
		}
		if (p[0] == 221 && p[1] >= 4 && p[2] == 0 && p[3] == 0x90 &&
		    p[4] == 0x4c && (p[5] == 0x33 || p[5] == 0x34))
			return EINVAL; /* No vendor HT negotiation in this profile. */
		p += size;
		len -= size;
	}
	return 0;
}

int
ieee80211_ht20_response(struct ieee80211com *ic, struct ieee80211_node *ni,
    int subtype, const struct ieee80211_ht20_node *offer,
    const uint8_t *p, size_t len, int wme)
{
	const struct ieee80211_ht20_node *h = offer;
	struct ieee80211_ht20_node candidate;
	const uint8_t *cap = NULL, *op = NULL;
	uint16_t rx, basic, caps, tx;
	size_t size;
	unsigned i, smps;

	if (!h->offered)
		return 0; /* An unsolicited HT IE never activates a legacy station. */
	if (!ht20_current(ic, ni, h) || subtype != h->subtype + 0x10 ||
	    ieee80211_ht20_check_ies(p, len) != 0)
		goto bad;
	while (len != 0) {
		if (len < 2 || p[1] > len - 2)
			goto bad;
		size = (size_t)p[1] + 2;
		if (p[0] == 45) {
			if (cap != NULL || p[1] != 26)
				goto bad;
			cap = p + 2;
		} else if (p[0] == 61) {
			if (op != NULL || p[1] != 22)
				goto bad;
			op = p + 2;
		}
		p += size;
		len -= size;
	}
	if (cap == NULL && op == NULL) {
		ieee80211_ht20_reset(ni); /* Successful legacy response. */
		return 0;
	}
	if (!wme || cap == NULL || op == NULL ||
	    op[0] != ieee80211_chan2ieee(ic, ic->ic_curchan))
		goto bad;
	rx = (uint16_t)cap[3] | (uint16_t)cap[4] << 8;
	if ((rx & 0xff) != 0xff)
		goto bad;
	basic = (uint16_t)op[6] | (uint16_t)op[7] << 8;
	for (i = 8; i < 22; i++)
		if (op[i] != 0) /* Basic MCS outside0..15 is unsupported. */
			goto bad;
	if ((basic & ~h->profile.rx_mcs) != 0)
		goto bad;
	caps = (uint16_t)cap[0] | (uint16_t)cap[1] << 8;
	smps = (caps >> 2) & 3;
	if (smps == 2)
		goto bad;
	tx = rx & h->profile.tx_mcs;
	if (smps != 3)
		tx &= 0xff; /* No implicit RTS policy for multi-stream SMPS. */
	if (tx == 0)
		goto bad;
	candidate = *h;
	candidate.offered = 0;
	candidate.tx_mcs = tx;
	candidate.peer_rx_mcs = rx;
	candidate.basic_mcs = basic;
	candidate.peer_cap = caps;
	candidate.operation = (uint16_t)op[2] | (uint16_t)op[3] << 8;
	memcpy(candidate.peer_htcap, cap, sizeof(candidate.peer_htcap));
	memcpy(candidate.peer_htop, op, sizeof(candidate.peer_htop));
	candidate.valid = 1;
	ni->ni_ht20 = candidate;
	/* The current provider admitted the bounded A-MSDU receive consumer. */
	ni->ni_flags |= IEEE80211_NODE_AMSDU_RX;
	return 0;
bad:
	ieee80211_ht20_reset(ni);
	return EINVAL;
}

/*
 * Primary20 mixed-format does not need RTS solely for operation2/non-GF.
 * Keep a narrower envelope than the wire standard: no greenfield/SMPS,
 * RIFS/PSMP/PCO/STBC/dual protection or other operation selectors. The native
 * consumer must also qualify frame length, threshold and TX aggregation.
 */
int
ieee80211_ht20_tx_protection(struct ieee80211com *ic, struct ieee80211_node *ni,
    struct ieee80211_ht20_protection *result)
{
	const struct ieee80211_ht20_node *h;
	const uint8_t *op;
	uint16_t cap, operation;

	memset(result, 0, sizeof(*result));
	if (ni == NULL || !ni->ni_ht20.valid ||
	    !ht20_current(ic, ni, &ni->ni_ht20))
		return EINVAL;
	h = &ni->ni_ht20;
	op = h->peer_htop;
	cap = (uint16_t)h->peer_htcap[0] | (uint16_t)h->peer_htcap[1] << 8;
	operation = (uint16_t)op[2] | (uint16_t)op[3] << 8;
	if (cap != h->peer_cap || operation != h->operation ||
	    (cap & 0x1c) != 0x0c || (operation != 0 && operation != 6) ||
	    (op[1] != 0 && op[1] != 5 && op[1] != 7) ||
	    op[4] != 0 || op[5] != 0 ||
	    op[0] != ieee80211_chan2ieee(ic, ic->ic_curchan) ||
	    (ic->ic_flags & IEEE80211_F_USEPROT) != 0)
		return EOPNOTSUPP;
	result->generation = h->profile.generation;
	result->operation = operation;
	result->peer_cap = cap;
	result->secondary = op[1];
	result->qualification = IEEE80211_HT20_PROT_MF20_NO_RTS;
	return 0;
}

/*
 * An accepted response fixes the peer HT contract until reassociation. Check
 * matching beacons before the legacy parser can return on a malformed TLV.
 * The caller owns the normal INIT request after this synchronous purge.
 */
int
ieee80211_ht20_beacon(struct ieee80211com *ic, struct ieee80211_node *ni,
    const struct ieee80211_frame *wh, const uint8_t *p, size_t len)
{
	const struct ieee80211_ht20_node *h = &ni->ni_ht20;
	const uint8_t *cap = NULL, *op = NULL;
	size_t size;

	if (!h->valid || ic->ic_opmode != IEEE80211_M_STA ||
	    (ic->ic_state != IEEE80211_S_ASSOC &&
	     ic->ic_state != IEEE80211_S_RUN) || ni != ic->ic_bss ||
	    !IEEE80211_ADDR_EQ(wh->i_addr2, ni->ni_macaddr) ||
	    !IEEE80211_ADDR_EQ(wh->i_addr3, ni->ni_bssid))
		return 0;
	if (!ht20_current(ic, ni, h) || len < 12)
		goto bad;
	p += 12;
	len -= 12;
	while (len != 0) {
		if (len < 2 || p[1] > len - 2)
			goto bad;
		size = (size_t)p[1] + 2;
		if (p[0] == 45) {
			if (cap != NULL || p[1] != sizeof(h->peer_htcap))
				goto bad;
			cap = p + 2;
		} else if (p[0] == 61) {
			if (op != NULL || p[1] != sizeof(h->peer_htop))
				goto bad;
			op = p + 2;
		}
		p += size;
		len -= size;
	}
	if (cap == NULL || op == NULL ||
	    memcmp(cap, h->peer_htcap, sizeof(h->peer_htcap)) != 0 ||
	    memcmp(op, h->peer_htop, sizeof(h->peer_htop)) != 0)
		goto bad;
	return 0;
bad:
	ieee80211_ht20_reset(ni);
	return EINVAL;
}

int
ieee80211_ht20_state_request(struct ieee80211com *ic, int state)
{
	struct ieee80211_node *ni = ic->ic_bss;

	if (ni == NULL || (!ni->ni_ht20.valid && !ni->ni_ht20.offered &&
	    ni->ni_ht20.profile.generation == 0))
		return 0;
	if (state != IEEE80211_S_RUN) {
		ieee80211_ht20_reset(ni);
		return 0;
	}
	if ((ni->ni_ht20.valid && !ht20_current(ic, ni, &ni->ni_ht20)) ||
	    ni->ni_ht20.offered) {
		ieee80211_ht20_reset(ni);
		return EINVAL;
	}
	return 0;
}
