/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD; extract the bounded AICWF VHT IE decoder, AI-assisted. */
#include <sys/types.h>
#include <sys/systm.h>
#include <net80211/ieee80211_vht_ies.h>

static uint16_t
vht_get16(const uint8_t *p)
{

	return p[0] | (uint16_t)p[1] << 8;
}

static void
vht_put16(uint8_t *p, uint16_t value)
{

	p[0] = value;
	p[1] = value >> 8;
}

int
ieee80211_vht_ies_parse(struct ieee80211_vht_ies *ies, const uint8_t *p,
    size_t len)
{
	struct ieee80211_vht_ies pending = {0};
	size_t n;

	if (ies == NULL)
		return -1;
	memset(ies, 0, sizeof(*ies));
	if (p == NULL && len != 0)
		return -1;
	while (len != 0) {
		if (len < 2 || p[1] > len - 2)
			return -1;
		n = p[1];
		if (p[0] == 191) {
			if (n != IEEE80211_VHT_CAP_LEN || pending.cap_present)
				return -1;
			pending.cap_present = true;
			pending.cap.info = (uint32_t)vht_get16(p + 2) |
			    (uint32_t)vht_get16(p + 4) << 16;
			pending.cap.rx_map = vht_get16(p + 6);
			pending.cap.rx_highest = vht_get16(p + 8);
			pending.cap.tx_map = vht_get16(p + 10);
			pending.cap.tx_highest = vht_get16(p + 12);
		} else if (p[0] == 192) {
			if (n != IEEE80211_VHT_OPERATION_LEN ||
			    pending.operation_present)
				return -1;
			pending.operation_present = true;
			pending.operation.width = p[2];
			pending.operation.seg0 = p[3];
			pending.operation.seg1 = p[4];
			pending.operation.basic_map = vht_get16(p + 5);
		}
		p += n + 2;
		len -= n + 2;
	}
	*ies = pending;
	return 0;
}

int
ieee80211_vht_cap_encode(uint8_t *p, size_t len,
    const struct ieee80211_vht_cap *cap)
{
	struct ieee80211_vht_cap value;

	if (p == NULL || cap == NULL || len < IEEE80211_VHT_CAP_LEN)
		return -1;
	value = *cap;
	vht_put16(p, value.info);
	vht_put16(p + 2, value.info >> 16);
	vht_put16(p + 4, value.rx_map);
	vht_put16(p + 6, value.rx_highest);
	vht_put16(p + 8, value.tx_map);
	vht_put16(p + 10, value.tx_highest);
	return 0;
}

int
ieee80211_vht_operation_encode(uint8_t *p, size_t len,
    const struct ieee80211_vht_operation *operation)
{
	struct ieee80211_vht_operation value;

	if (p == NULL || operation == NULL || len < IEEE80211_VHT_OPERATION_LEN)
		return -1;
	value = *operation;
	p[0] = value.width;
	p[1] = value.seg0;
	p[2] = value.seg1;
	vht_put16(p + 3, value.basic_map);
	return 0;
}

uint16_t
ieee80211_vht_mcs_intersect(uint16_t a, uint16_t b)
{
	uint16_t result = 0;
	unsigned shift, x, y, common;

	for (shift = 0; shift < 16; shift += 2) {
		x = (a >> shift) & 3;
		y = (b >> shift) & 3;
		common = x == 3 || y == 3 ? 3 : (x < y ? x : y);
		result |= (uint16_t)(common << shift);
	}
	return result;
}

struct ieee80211_vht_mcs_maps
ieee80211_vht_mcs_candidates(struct ieee80211_vht_mcs_maps local,
    struct ieee80211_vht_mcs_maps peer)
{
	struct ieee80211_vht_mcs_maps candidate;

	candidate.tx_map = ieee80211_vht_mcs_intersect(local.tx_map, peer.rx_map);
	candidate.rx_map = ieee80211_vht_mcs_intersect(local.rx_map, peer.tx_map);
	return candidate;
}

bool
ieee80211_vht_mcs_meets(uint16_t supported, uint16_t required)
{
	unsigned shift, have, need;

	for (shift = 0; shift < 16; shift += 2) {
		have = (supported >> shift) & 3;
		need = (required >> shift) & 3;
		if (need != 3 && (have == 3 || have < need))
			return false;
	}
	return true;
}

/* Find the single HT Capabilities and HT Operation payloads, if present. */
static int
vht_ht_ies(const uint8_t *ies, size_t len, const uint8_t **htcap,
    const uint8_t **htop)
{
	const uint8_t *p = ies, *end = ies + len;
	size_t n;

	*htcap = *htop = NULL;
	while (p < end) {
		if ((size_t)(end - p) < 2 || p[1] > (size_t)(end - p) - 2)
			return -1;
		n = (size_t)p[1] + 2;
		if (p[0] == 45) {
			if (*htcap != NULL || p[1] != 26)
				return -1;
			*htcap = p + 2;
		} else if (p[0] == 61) {
			if (*htop != NULL || p[1] != 22)
				return -1;
			*htop = p + 2;
		}
		p += n;
	}
	return 0;
}

/* 5 GHz channel blocks of IEEE 802.11-2016 Annex E, by center index. */
static int
vht_center(int center, unsigned width)
{
	static const uint8_t c40[] = { 38, 46, 54, 62, 102, 110, 118, 126,
	    134, 142, 151, 159 };
	static const uint8_t c80[] = { 42, 58, 106, 122, 138, 155 };
	static const uint8_t c160[] = { 50, 114, 163 };
	const uint8_t *c = width == 40 ? c40 : width == 80 ? c80 : c160;
	size_t i, n = width == 40 ? sizeof(c40) : width == 80 ?
	    sizeof(c80) : sizeof(c160);

	for (i = 0; i < n; i++)
		if (c[i] == center)
			return 1;
	return 0;
}

int
ieee80211_vht_channel_derive(const uint8_t *ies, size_t len,
    unsigned primary, struct ieee80211_vht_channel *out)
{
	struct ieee80211_vht_ies vht;
	const uint8_t *htcap, *htop;
	int p = (int)primary, s0, s1, d, center, offset;
	unsigned sco, width = 80;

	if (out == NULL)
		return -1;
	memset(out, 0, sizeof(*out));
	/* Also rejects an unknown channel's (unsigned)-1 index. HT
	 * Capabilities is only required to be well formed here. */
	if (primary > 196 || ieee80211_vht_ies_parse(&vht, ies, len) != 0 ||
	    !vht.operation_present ||
	    vht_ht_ies(ies, len, &htcap, &htop) != 0 || htop == NULL ||
	    htop[0] != primary || (htop[1] & 0x04) == 0)
		return -1;
	sco = htop[1] & 0x03;
	if (sco != 1 && sco != 3)
		return -1;
	s0 = vht.operation.seg0;
	s1 = vht.operation.seg1;
	d = s1 > s0 ? s1 - s0 : s0 - s1;
	switch (vht.operation.width) {
	case 0:
		/* HT 40 MHz; both segments are reserved. */
		width = 40;
		center = sco == 1 ? p + 2 : p - 2;
		break;
	case 1:
		/* Segment1 names the 160 MHz center or the secondary 80. */
		if (s1 != 0 && !(d == 8 && vht_center(s1, 160)) &&
		    !(d > 16 && vht_center(s1, 80)))
			return -1;
		center = s0;
		break;
	case 2:
		if (!vht_center(s0, 160))
			return -1;
		center = p < s0 ? s0 - 8 : s0 + 8;
		break;
	case 3:
		if (d <= 16 || !vht_center(s1, 80))
			return -1;
		center = s0;
		break;
	default:
		return -1;
	}
	if (!vht_center(center, width))
		return -1;
	offset = p - center;
	if (width == 80) {
		/* The HT secondary must be this primary's 40 MHz partner. */
		if (offset == -6 || offset == 2) {
			if (sco != 1)
				return -1;
		} else if (offset == -2 || offset == 6) {
			if (sco != 3)
				return -1;
		} else
			return -1;
	}
	out->primary = (uint8_t)primary;
	out->center = (uint8_t)center;
	out->width_mhz = (uint8_t)width;
	return 0;
}

/*
 * Validate the qualified local profile (one stream with MCS0-7 or MCS0-9,
 * or two streams with MCS0-9; equal RX and TX maps; at most the RX LDPC
 * and SGI80 capability bits) against one
 * AP-derived operating channel. Each negotiated direction is the
 * intersection with the peer and must keep NSS1. Raw scan observations
 * are rechecked here; they do not constitute negotiated state.
 *
 * Deliberate deviation from IEEE 802.11ac-2013 §10.39.7: the AP's Basic
 * VHT-MCS and NSS set is not enforced. Many APs list their own full
 * support there (e.g. 0xff9a: NSS1, NSS2 and NSS4 MCS0-9, NSS3 MCS0-8),
 * which would keep every
 * NSS1 MCS0-7 station out of VHT. The association request offers VHT
 * only beside a nonempty legacy Basic Rate Set that the station supports,
 * so group-addressed frames stay non-HT (§9.7.5.3), and each MCS map
 * direction is still checked.
 */
int
ieee80211_vht_assoc_candidate(const struct ieee80211_vht_cap *local,
    const uint8_t *ies, size_t len, const struct ieee80211_vht_channel *chan)
{
	struct ieee80211_vht_ies peer;
	struct ieee80211_vht_channel derived;
	const uint8_t *htcap, *htop;

	if (local == NULL || chan == NULL || ies == NULL || len == 0 ||
	    len > 73 || (local->info & ~0x30U) != 0 ||
	    local->rx_map != local->tx_map ||
	    (local->rx_map != 0xfffc && local->rx_map != 0xfffe &&
	     local->rx_map != 0xfffa) ||
	    local->rx_highest != 0 || local->tx_highest != 0 ||
	    ieee80211_vht_ies_parse(&peer, ies, len) != 0 ||
	    !peer.cap_present || !peer.operation_present ||
	    /* Each negotiated direction keeps at least NSS1 MCS0-7. */
	    (ieee80211_vht_mcs_intersect(local->tx_map, peer.cap.rx_map) & 3) == 3 ||
	    (ieee80211_vht_mcs_intersect(local->rx_map, peer.cap.tx_map) & 3) == 3 ||
	    vht_ht_ies(ies, len, &htcap, &htop) != 0 || htcap == NULL ||
	    (htcap[0] & 0x02) == 0 || htcap[3] != 0xff ||
	    ieee80211_vht_channel_derive(ies, len, chan->primary,
	    &derived) != 0 || derived.center != chan->center ||
	    derived.width_mhz != chan->width_mhz)
		return -1;
	return 0;
}
