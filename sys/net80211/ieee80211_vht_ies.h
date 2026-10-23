/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD shared bounded VHT wire values, AI-assisted. */
#ifndef _NET80211_IEEE80211_VHT_IES_H_
#define _NET80211_IEEE80211_VHT_IES_H_

#include <sys/types.h>

#define IEEE80211_VHT_CAP_LEN		12
#define IEEE80211_VHT_OPERATION_LEN	5

/* Raw wire values. Highest fields retain all reserved/extended-NSS bits. */
struct ieee80211_vht_cap {
	uint32_t info;
	uint16_t rx_map;
	uint16_t rx_highest;
	uint16_t tx_map;
	uint16_t tx_highest;
};

struct ieee80211_vht_operation {
	uint8_t width;
	uint8_t seg0;
	uint8_t seg1;
	uint16_t basic_map;
};

struct ieee80211_vht_ies {
	bool cap_present;
	bool operation_present;
	struct ieee80211_vht_cap cap;
	struct ieee80211_vht_operation operation;
};

/*
 * Decode a complete bounded TLV window. Return 0 on success, -1 on failure.
 * Reject malformed framing, wrong VHT lengths and duplicate VHT elements.
 * Empty input succeeds with no presence flags. Failure clears all output.
 * A NULL input is valid only with zero length. A NULL output fails.
 * Decode input and output must not overlap.
 * This is structural observation, with no admission or PHY/rate inference.
 */
int ieee80211_vht_ies_parse(struct ieee80211_vht_ies *, const uint8_t *,
    size_t);

/*
 * Encode payloads only, without TLV headers, from explicit raw values.
 * Return 0 on success, -1 for NULL pointers or insufficient output space.
 * Failure changes no output. Success writes exactly 12 or 5 bytes.
 * Input values are read before writes, so input/output overlap is valid.
 * Capability width-set zero excludes 160 MHz; it does not mean 20-only.
 * Operation width zero refers to HT 20/40 MHz, not an automatic 20 MHz mode.
 */
int ieee80211_vht_cap_encode(uint8_t *, size_t,
    const struct ieee80211_vht_cap *);
int ieee80211_vht_operation_encode(uint8_t *, size_t,
    const struct ieee80211_vht_operation *);

#endif /* _NET80211_IEEE80211_VHT_IES_H_ */
