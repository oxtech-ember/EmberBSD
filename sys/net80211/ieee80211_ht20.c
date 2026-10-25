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

/*
 * The pairwise cipher this station requests: its own RSN element in the
 * optional IEs, which wpa_supplicant installs before association. Its
 * driver never sets the unicast cipher ioctl, so ni_rsn keeps the WEP
 * default and cannot decide. Only one well-formed RSN element (version 1)
 * with a single CCMP-128 pairwise suite qualifies; a WPA vendor element,
 * TKIP or any additional pairwise suite does not.
 */
static int
ht20_rsn_ccmp(const struct ieee80211com *ic)
{
	const uint8_t *p = ic->ic_opt_ie, *rsn = NULL;
	size_t len = ic->ic_opt_ie_len, size;

	if (p == NULL)
		return 0;
	while (len != 0) {
		if (len < 2 || p[1] > len - 2)
			return 0;
		size = (size_t)p[1] + 2;
		if (p[0] == 48) {
			if (rsn != NULL)
				return 0;
			rsn = p;
		}
		p += size;
		len -= size;
	}
	/* Version, group suite, pairwise count 1 and pairwise 00-0F-AC:4. */
	return rsn != NULL && rsn[1] >= 12 && rsn[2] == 1 && rsn[3] == 0 &&
	    rsn[8] == 1 && rsn[9] == 0 && rsn[10] == 0x00 &&
	    rsn[11] == 0x0f && rsn[12] == 0xac && rsn[13] == 4;
}

int
ieee80211_ht20_prepare(struct ieee80211com *ic, struct ieee80211_node *ni,
    int wme, struct ieee80211_ht20_profile *p)
{

	if (!wme || ic->ic_wme.wme_sta_apply == NULL || ic->ic_opmode != IEEE80211_M_STA ||
	    ic->ic_state != IEEE80211_S_ASSOC || ni != ic->ic_bss ||
	    ni->ni_chan != ic->ic_curchan ||
	    ((ic->ic_flags & IEEE80211_F_PRIVACY) != 0 && !ht20_rsn_ccmp(ic)))
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
    int type, const struct ieee80211_ht20_profile *profile,
    const struct ieee80211_vht_cap *vht,
    const struct ieee80211_vht_channel *vht_chan)
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
	h->assoc_attempt = ni->ni_wme_attempt;
	h->security = (ic->ic_flags & IEEE80211_F_PRIVACY) != 0;
	h->offered = 1;
	ic->ic_ht20_diag.offers++;
	memset(&h->vht, 0, sizeof(h->vht));
	/* Bind the offer to the channel the request admitted. */
	if (vht != NULL && vht_chan != NULL) {
		h->vht.local = *vht;
		h->vht.channel = *vht_chan;
		h->vht.offered = 1;
		ic->ic_ht20_diag.vht_offers++;
	}
}

/*
 * The first failing clause of the committed HT/VHT contract, 1-based in
 * this order, or 0 when current; numeric diagnostics use the value.
 */
static unsigned
ht20_stale(struct ieee80211com *ic, struct ieee80211_node *ni,
    const struct ieee80211_ht20_node *h)
{
	struct ieee80211_ht20_profile local;

	if (ic->ic_opmode != IEEE80211_M_STA ||
	    (ic->ic_state != IEEE80211_S_ASSOC && ic->ic_state != IEEE80211_S_RUN))
		return 1;
	if ((ic->ic_flags & IEEE80211_F_WME) == 0 ||
	    (ic->ic_caps & IEEE80211_C_WME) == 0)
		return 2;
	if (ic->ic_wme.wme_sta_apply == NULL)
		return 3;
	if ((ni->ni_flags & (IEEE80211_NODE_QOS | IEEE80211_NODE_WME_VALID)) !=
	    (IEEE80211_NODE_QOS | IEEE80211_NODE_WME_VALID))
		return 4;
	if (h->security != ((ic->ic_flags & IEEE80211_F_PRIVACY) != 0))
		return 5;
	if ((ic->ic_flags & IEEE80211_F_PRIVACY) != 0 && !ht20_rsn_ccmp(ic))
		return 6;
	if (ni != ic->ic_bss)
		return 7;
	if (h->offer_attempt != h->attempt || h->attempt != ni->ni_ht20.attempt)
		return 8;
	if (h->assoc_attempt != ni->ni_wme_attempt)
		return 9;
	if (h->channel != ic->ic_curchan || ni->ni_chan != ic->ic_curchan)
		return 10;
	if (!IEEE80211_ADDR_EQ(h->bssid, ni->ni_bssid))
		return 11;
	if (!ht20_local(ic, &local))
		return 12;
	if (local.generation != h->profile.generation)
		return 13;
	if (local.rx_mcs != h->profile.rx_mcs ||
	    local.tx_mcs != h->profile.tx_mcs || local.ampdu != h->profile.ampdu)
		return 14;
	return 0;
}

static int
ht20_current(struct ieee80211com *ic, struct ieee80211_node *ni,
    const struct ieee80211_ht20_node *h)
{
	return ht20_stale(ic, ni, h) == 0;
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
		/*
		 * Pre-standard Broadcom HT elements (00:90:4c:33/34) in a
		 * response duplicate the standard ones, which alone decide;
		 * many APs still send them. They are ignored, never parsed.
		 */
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
	struct ieee80211_vht_ies vht;
	struct ieee80211_vht_cap local_vht;
	struct ieee80211_vht_channel vht_chan;
	uint8_t phy_ies[IEEE80211_PHY_IES_MAXLEN];
	const uint8_t *cap = NULL, *op = NULL;
	uint16_t rx, basic, caps, tx;
	size_t size, phy_len;
	unsigned i, smps;
	int vht_phy_overflow;

	phy_len = 0;
	vht_phy_overflow = 0;
	if (!h->offered) {
		/* An unsolicited HT IE never activates a legacy station. */
		ic->ic_ht20_diag.unoffered++;
		return 0;
	}
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
		/* The association response also carries Rates/WME/RSN IEs. Keep
		 * only the bounded PHY IEs for the strict VHT candidate validator. */
		if (p[0] == 45 || p[0] == 61 || p[0] == 191 || p[0] == 192) {
			if (vht_phy_overflow ||
			    size > sizeof(phy_ies) - phy_len)
				vht_phy_overflow = 1;
			else {
				memcpy(phy_ies + phy_len, p, size);
				phy_len += size;
			}
		}
		p += size;
		len -= size;
	}
	if (cap == NULL && op == NULL) {
		ieee80211_ht20_reset(ni); /* Successful legacy response. */
		ic->ic_ht20_diag.legacy++;
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
	/* Commit VHT only when this response matches a queued local offer. */
	candidate.vht.offered = 0;
	candidate.vht.valid = 0;
	if (!vht_phy_overflow && h->vht.offered &&
	    ic->ic_vht_admit != NULL &&
	    ieee80211_vht_channel_derive(phy_ies, phy_len,
	    ieee80211_chan2ieee(ic, ic->ic_curchan), &vht_chan) == 0 &&
	    ic->ic_vht_admit(ic, &vht_chan, &local_vht) == 0 &&
	    ieee80211_vht_ies_parse(&vht, phy_ies, phy_len) == 0 &&
	    vht.cap_present && vht.operation_present &&
	    ieee80211_vht_assoc_candidate(&local_vht, phy_ies, phy_len,
	    &vht_chan) == 0 &&
	    /* Commit only the channel and capability this offer carried. */
	    vht_chan.primary == h->vht.channel.primary &&
	    vht_chan.center == h->vht.channel.center &&
	    vht_chan.width_mhz == h->vht.channel.width_mhz &&
	    memcmp(&local_vht, &h->vht.local, sizeof(local_vht)) == 0) {
		candidate.vht.local = local_vht;
		candidate.vht.channel = vht_chan;
		candidate.vht.peer = vht.cap;
		candidate.vht.operation = vht.operation;
		candidate.vht.maps.tx_map = ieee80211_vht_mcs_intersect(
		    local_vht.tx_map, vht.cap.rx_map);
		candidate.vht.maps.rx_map = ieee80211_vht_mcs_intersect(
		    local_vht.rx_map, vht.cap.tx_map);
		candidate.vht.valid = 1;
	}
	ni->ni_ht20 = candidate;
	ic->ic_ht20_diag.ht++;
	if (candidate.vht.valid)
		ic->ic_ht20_diag.vht++;
	/* The current provider admitted the bounded A-MSDU receive consumer. */
	ni->ni_flags |= IEEE80211_NODE_AMSDU_RX;
	return 0;
bad:
	ieee80211_ht20_reset(ni);
	ic->ic_ht20_diag.bad++;
	return EINVAL;
}

/*
 * Primary20 mixed-format does not need RTS solely for operation2/non-GF.
 * Deliberate deviation: on 5 GHz, non-HT mixed mode 3 (0x0003, or 0x0007
 * with non-GF) is accepted without RTS/CTS. The upstream rtw88 driver
 * requests RTS only for use_rts or the length threshold and ignores the
 * HT Protection field. This relies on OFDM-only non-HT stations deferring
 * on the L-SIG of mixed-format HT and VHT PPDUs, and on the consumer
 * sending single unaggregated MPDUs without TXOP bursts or length RTS.
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
	    (cap & 0x1c) != 0x0c || (operation != 0 && operation != 3 &&
	    operation != 6 && operation != 7) ||
	    ((operation == 3 || operation == 7) && op[0] <= 14) ||
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
	const uint8_t *cap = NULL, *op = NULL, *vop = NULL;
	uint8_t vht_op[IEEE80211_VHT_OPERATION_LEN];
	size_t size;
	int vop_bad = 0;
	uint8_t reason = IEEE80211_HT20_RESET_STALE;

	if (!h->valid || ic->ic_opmode != IEEE80211_M_STA ||
	    (ic->ic_state != IEEE80211_S_ASSOC &&
	     ic->ic_state != IEEE80211_S_RUN) || ni != ic->ic_bss ||
	    !IEEE80211_ADDR_EQ(wh->i_addr2, ni->ni_macaddr) ||
	    !IEEE80211_ADDR_EQ(wh->i_addr3, ni->ni_bssid))
		return 0;
	if (!ht20_current(ic, ni, h))
		goto bad;
	reason = IEEE80211_HT20_RESET_FRAMING;
	if (len < 12)
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
		} else if (p[0] == 192) {
			/* Only a VHT node depends on this element. */
			if (vop != NULL || p[1] != IEEE80211_VHT_OPERATION_LEN)
				vop_bad = 1;
			vop = p + 2;
		}
		p += size;
		len -= size;
	}
	reason = IEEE80211_HT20_RESET_HT;
	if (cap == NULL || op == NULL ||
	    memcmp(cap, h->peer_htcap, sizeof(h->peer_htcap)) != 0 ||
	    memcmp(op, h->peer_htop, sizeof(h->peer_htop)) != 0)
		goto bad;
	reason = IEEE80211_HT20_RESET_VHT;
	/* A negotiated VHT width is bound to the AP's VHT Operation. */
	if (h->vht.valid && (vop == NULL || vop_bad ||
	    ieee80211_vht_operation_encode(vht_op, sizeof(vht_op),
	    &h->vht.operation) != 0 ||
	    memcmp(vop, vht_op, sizeof(vht_op)) != 0))
		goto bad;
	return 0;
bad:
	ieee80211_ht20_reset(ni);
	ic->ic_ht20_diag.resets++;
	ic->ic_ht20_diag.reset_reason = reason;
	return EINVAL;
}

int
ieee80211_ht20_state_request(struct ieee80211com *ic, int state)
{
	struct ieee80211_node *ni = ic->ic_bss;
	unsigned reason;

	if (ni == NULL || (!ni->ni_ht20.valid && !ni->ni_ht20.offered &&
	    ni->ni_ht20.profile.generation == 0))
		return 0;
	if (state != IEEE80211_S_RUN) {
		ieee80211_ht20_reset(ni);
		return 0;
	}
	/* An unanswered offer reports 15; a stale node its first clause. */
	reason = ni->ni_ht20.offered ? 15 :
	    ni->ni_ht20.valid ? ht20_stale(ic, ni, &ni->ni_ht20) : 0;
	if (reason != 0) {
		ic->ic_ht20_diag.state_fail++;
		ic->ic_ht20_diag.state_reason = (uint8_t)reason;
		ieee80211_ht20_reset(ni);
		return EINVAL;
	}
	return 0;
}
