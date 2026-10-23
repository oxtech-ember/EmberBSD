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
