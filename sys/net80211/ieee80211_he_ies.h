/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD shared bounded HE (802.11ax) wire values, AI-assisted. */
#ifndef _NET80211_IEEE80211_HE_IES_H_
#define _NET80211_IEEE80211_HE_IES_H_

#include <sys/types.h>

/* Element Identifier Extension values in an element 255. */
#define IEEE80211_ELEMID_EXTENSION	255
#define IEEE80211_ELEMID_EXT_HE_CAP	35
#define IEEE80211_ELEMID_EXT_HE_OP	36

#define IEEE80211_HE_MAC_CAP_LEN	6
#define IEEE80211_HE_PHY_CAP_LEN	11
#define IEEE80211_HE_PPE_MAXLEN		25
/* Element body (after the length octet) of a HE Capabilities element
 * without 160 MHz maps and PPE thresholds: extension ID, MAC, PHY, maps. */
#define IEEE80211_HE_CAP_MINLEN		(1 + 6 + 11 + 4)
#define IEEE80211_HE_CAP_MAXLEN		(1 + 6 + 11 + 12 + IEEE80211_HE_PPE_MAXLEN)

/* PHY Capabilities Information octets and bits used by the shared owner. */
#define IEEE80211_HE_PHY0_CW_40_2G	0x02	/* B1: 40 MHz in 2.4 GHz */
#define IEEE80211_HE_PHY0_CW_40_80_5G	0x04	/* B2: 40/80 MHz in 5 GHz */
#define IEEE80211_HE_PHY0_CW_160_5G	0x08	/* B3: 160 MHz in 5 GHz */
#define IEEE80211_HE_PHY0_CW_80P80_5G	0x10	/* B4: 80+80 MHz in 5 GHz */
#define IEEE80211_HE_PHY1_LDPC		0x20	/* B13: LDPC in payload */
#define IEEE80211_HE_PHY6_PPE_PRESENT	0x80	/* B55: PPE Thresholds */
#define IEEE80211_HE_PHY9_PADDING_MASK	0xc0	/* B78-B79: nominal padding */
#define IEEE80211_HE_PHY9_PADDING_16US	0x80

/* HE Operation Parameters bits that add optional fields. */
#define IEEE80211_HE_OP_VHT_INFO	0x004000 /* B14 */
#define IEEE80211_HE_OP_COHOSTED	0x008000 /* B15 */
#define IEEE80211_HE_OP_6GHZ_INFO	0x020000 /* B17 */
#define IEEE80211_HE_OP_ER_SU_DISABLE	0x010000 /* B16 */

/* Raw wire values. Maps use the VHT two-bit encoding; entry 2 is MCS 0-11. */
struct ieee80211_he_cap {
	uint8_t mac[IEEE80211_HE_MAC_CAP_LEN];
	uint8_t phy[IEEE80211_HE_PHY_CAP_LEN];
	uint16_t rx_map80, tx_map80;	/* <= 80 MHz HE-MCS and NSS sets */
	uint8_t ppe_len;		/* 0 without PPE Thresholds */
	uint8_t ppe[IEEE80211_HE_PPE_MAXLEN];
};

struct ieee80211_he_operation {
	uint32_t params;		/* 24-bit HE Operation Parameters */
	uint8_t bss_color;		/* BSS Color Information octet */
	uint16_t basic_map;		/* Basic HE-MCS and NSS set */
};

struct ieee80211_he_ies {
	bool cap_present;
	bool operation_present;
	struct ieee80211_he_cap cap;
	struct ieee80211_he_operation operation;
};

/*
 * Decode a complete bounded TLV window. Return 0 on success, -1 on failure.
 * Malformed framing, a HE element whose length does not match the fields
 * its own bits announce, and a duplicate HE element fail. Other complete
 * TLVs, including other extension elements, are skipped. Failure clears
 * the output. A NULL input is valid only with zero length.
 */
int ieee80211_he_ies_parse(struct ieee80211_he_ies *, const uint8_t *,
    size_t);

/*
 * Encode a complete HE Capabilities element (ID, length, extension ID and
 * body) from explicit values. 160 and 80+80 MHz maps are not encoded, so
 * those width bits must be clear; PPE Thresholds follow the present bit.
 * Return the element length, or -1 for NULL pointers, inconsistent values
 * or insufficient space. Failure changes no output.
 */
int ieee80211_he_cap_encode(uint8_t *, size_t, const struct ieee80211_he_cap *);

/*
 * Qualified local station profile: one or two streams with MCS 0-7, 0-9 or
 * 0-11 and equal maps; no MAC capability; PHY capabilities limited to the
 * 2.4 GHz 40 MHz or 5 GHz 40/80 MHz width bit, LDPC and the nominal
 * packet padding; no PPE Thresholds. Return 0 or -1.
 */
int ieee80211_he_local_check(const struct ieee80211_he_cap *);

#endif /* _NET80211_IEEE80211_HE_IES_H_ */
