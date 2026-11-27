/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD explicit HE wire codec contracts, AI-assisted. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <net80211/ieee80211_he_ies.h>

static unsigned checks;
#define CHECK(e) do { if (!(e)) { \
	fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #e); exit(1); \
} checks++; } while (0)

/*
 * An observed 802.11ax/be AP: 160 MHz maps and 13 octets of PPE
 * Thresholds (NSTS 4, RU index bitmask 0xf: 7 + 4 * 4 * 6 = 103 bits).
 */
static const uint8_t ap_cap[] = {
	255, 39, 35,
	0x44, 0x10, 0x08, 0x1a, 0x05, 0x00,		/* MAC */
	0x0c, 0x20, 0xce, 0x92, 0x93, 0x1b, 0xaf, 0xd4,	/* PHY */
	0x11, 0x0c, 0x00,
	0xaa, 0xff, 0xaa, 0xff,				/* <= 80 MHz */
	0xaa, 0xff, 0xaa, 0xff,				/* 160 MHz */
	0x7b, 0x1c, 0xc7, 0x71, 0x1c, 0xc7, 0x71, 0x1c,	/* PPE */
	0xc7, 0x71, 0x1c, 0xc7, 0x71,
};

/* HE Operation with the VHT Operation Information present (B14). */
static const uint8_t ap_op[] = {
	255, 10, 36, 0xf4, 0x7f, 0x00, 0x2a, 0xfc, 0xff, 0x01, 0x3a, 0x00,
};

static size_t
window(uint8_t *out, const uint8_t *a, size_t an, const uint8_t *b,
    size_t bn)
{
	static const uint8_t other[] = { 255, 2, 59, 0x01, 221, 3, 1, 2, 3 };

	memcpy(out, other, sizeof(other));
	memcpy(out + sizeof(other), a, an);
	memcpy(out + sizeof(other) + an, b, bn);
	return sizeof(other) + an + bn;
}

static void
test_observed_ap(void)
{
	uint8_t w[128];
	struct ieee80211_he_ies he;
	size_t n = window(w, ap_cap, sizeof(ap_cap), ap_op, sizeof(ap_op));

	CHECK(ieee80211_he_ies_parse(&he, w, n) == 0);
	CHECK(he.cap_present && he.operation_present);
	CHECK(he.cap.mac[0] == 0x44 && he.cap.mac[4] == 0x05);
	CHECK(he.cap.phy[0] == 0x0c && he.cap.phy[10] == 0x00);
	CHECK(he.cap.rx_map80 == 0xffaa && he.cap.tx_map80 == 0xffaa);
	CHECK(he.cap.ppe_len == 13 && he.cap.ppe[0] == 0x7b &&
	    he.cap.ppe[12] == 0x71);
	CHECK(he.operation.params == 0x007ff4);
	CHECK(he.operation.bss_color == 0x2a);
	CHECK(he.operation.basic_map == 0xfffc);
	/* Empty and foreign-only windows succeed without presence. */
	CHECK(ieee80211_he_ies_parse(&he, NULL, 0) == 0 && !he.cap_present);
	CHECK(ieee80211_he_ies_parse(&he, w, 9) == 0 && !he.cap_present &&
	    !he.operation_present);
	CHECK(ieee80211_he_ies_parse(NULL, w, n) == -1);
	CHECK(ieee80211_he_ies_parse(&he, NULL, 1) == -1);
}

static void
test_bad_lengths(void)
{
	uint8_t w[128], c[sizeof(ap_cap)], o[sizeof(ap_op)];
	struct ieee80211_he_ies he;
	size_t n;

	/* Every shorter HE Capabilities body is inconsistent with its bits. */
	for (size_t cut = 1; cut < sizeof(ap_cap) - 3; cut++) {
		memcpy(c, ap_cap, sizeof(c));
		c[1] -= cut;
		n = window(w, c, sizeof(c) - cut, ap_op, sizeof(ap_op));
		CHECK(ieee80211_he_ies_parse(&he, w, n) == -1);
		CHECK(!he.cap_present && !he.operation_present);
	}
	/* One extra octet after the PPE Thresholds. */
	memcpy(c, ap_cap, sizeof(c));
	{
		uint8_t big[sizeof(ap_cap) + 1];

		memcpy(big, ap_cap, sizeof(ap_cap));
		big[1]++;
		big[sizeof(ap_cap)] = 0;
		n = window(w, big, sizeof(big), ap_op, sizeof(ap_op));
		CHECK(ieee80211_he_ies_parse(&he, w, n) == -1);
	}
	/* Without the PPE present bit the PPE octets are surplus. */
	c[3 + 6 + 6] &= ~IEEE80211_HE_PHY6_PPE_PRESENT;
	n = window(w, c, sizeof(c), ap_op, sizeof(ap_op));
	CHECK(ieee80211_he_ies_parse(&he, w, n) == -1);
	/* HE Operation: the VHT Operation Information follows B14. */
	memcpy(o, ap_op, sizeof(o));
	o[4] &= ~0x40;
	n = window(w, ap_cap, sizeof(ap_cap), o, sizeof(o));
	CHECK(ieee80211_he_ies_parse(&he, w, n) == -1);
	o[4] |= 0x40 | 0x80;	/* Co-hosted BSSID indicator absent. */
	n = window(w, ap_cap, sizeof(ap_cap), o, sizeof(o));
	CHECK(ieee80211_he_ies_parse(&he, w, n) == -1);
	/* Duplicates fail; truncated framing fails. */
	n = window(w, ap_cap, sizeof(ap_cap), ap_cap, sizeof(ap_cap));
	CHECK(ieee80211_he_ies_parse(&he, w, n) == -1);
	n = window(w, ap_op, sizeof(ap_op), ap_op, sizeof(ap_op));
	CHECK(ieee80211_he_ies_parse(&he, w, n) == -1);
	n = window(w, ap_cap, sizeof(ap_cap), ap_op, sizeof(ap_op));
	CHECK(ieee80211_he_ies_parse(&he, w, n - 1) == -1);
	/* An empty extension element is skipped, not read. */
	w[0] = 255; w[1] = 0;
	CHECK(ieee80211_he_ies_parse(&he, w, 2) == 0 && !he.cap_present);
}

static struct ieee80211_he_cap
local(uint16_t map, uint8_t cw)
{
	struct ieee80211_he_cap cap;

	memset(&cap, 0, sizeof(cap));
	cap.phy[0] = cw;
	cap.phy[1] = IEEE80211_HE_PHY1_LDPC;
	cap.phy[9] = IEEE80211_HE_PHY9_PADDING_16US;
	cap.rx_map80 = cap.tx_map80 = map;
	return cap;
}

static void
test_encode_and_local(void)
{
	static const uint16_t good[] = {
		0xfffc, 0xfffd, 0xfffe, 0xfff0, 0xfff5, 0xfffa
	};
	static const uint16_t bad[] = {
		0xffff, 0xfff3, 0xfff6, 0xffea, 0xff55, 0xfffb
	};
	struct ieee80211_he_cap cap = local(0xfffa,
	    IEEE80211_HE_PHY0_CW_40_80_5G), back;
	struct ieee80211_he_ies he;
	uint8_t buf[64], keep[64];
	int n;

	n = ieee80211_he_cap_encode(buf, sizeof(buf), &cap);
	CHECK(n == 2 + IEEE80211_HE_CAP_MINLEN);
	CHECK(buf[0] == 255 && buf[1] == IEEE80211_HE_CAP_MINLEN &&
	    buf[2] == 35 && buf[3 + 6] == IEEE80211_HE_PHY0_CW_40_80_5G);
	CHECK(ieee80211_he_ies_parse(&he, buf, (size_t)n) == 0 &&
	    he.cap_present && !he.operation_present);
	back = he.cap;
	CHECK(memcmp(&back, &cap, sizeof(cap)) == 0);
	/* Insufficient space or an unencodable width changes nothing. */
	memset(buf, 0x5a, sizeof(buf));
	memcpy(keep, buf, sizeof(buf));
	CHECK(ieee80211_he_cap_encode(buf, (size_t)n - 1, &cap) == -1);
	cap.phy[0] |= IEEE80211_HE_PHY0_CW_160_5G;
	CHECK(ieee80211_he_cap_encode(buf, sizeof(buf), &cap) == -1);
	CHECK(memcmp(buf, keep, sizeof(buf)) == 0);
	CHECK(ieee80211_he_cap_encode(NULL, sizeof(buf), &cap) == -1);
	CHECK(ieee80211_he_cap_encode(buf, sizeof(buf), NULL) == -1);

	for (size_t i = 0; i < sizeof(good) / sizeof(good[0]); i++) {
		cap = local(good[i], 0);
		CHECK(ieee80211_he_local_check(&cap) == 0);
	}
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		cap = local(bad[i], 0);
		CHECK(ieee80211_he_local_check(&cap) == -1);
	}
	cap = local(0xfffa, 0);
	cap.tx_map80 = 0xfffe;
	CHECK(ieee80211_he_local_check(&cap) == -1);
	/* Only the width, LDPC and padding bits are qualified. */
	for (unsigned octet = 0; octet < 6 + 11; octet++) {
		for (unsigned bit = 0; bit < 8; bit++) {
			int want;

			cap = local(0xfffa, 0);
			cap.phy[9] = 0;
			cap.phy[1] = 0;
			if (octet < 6)
				cap.mac[octet] = 1U << bit;
			else
				cap.phy[octet - 6] = 1U << bit;
			want = octet >= 6 &&
			    ((octet == 6 && (bit == 1 || bit == 2)) ||
			    (octet == 7 && bit == 5) ||
			    (octet == 15 && (bit == 6 || bit == 7))) ? 0 : -1;
			CHECK(ieee80211_he_local_check(&cap) == want);
		}
	}
	cap = local(0xfffa, 0);
	cap.phy[9] = IEEE80211_HE_PHY9_PADDING_MASK;	/* Reserved value. */
	CHECK(ieee80211_he_local_check(&cap) == -1);
	cap = local(0xfffa, 0);
	cap.phy[6] = IEEE80211_HE_PHY6_PPE_PRESENT;
	CHECK(ieee80211_he_local_check(&cap) == -1);
	CHECK(ieee80211_he_local_check(NULL) == -1);
}

int
main(void)
{

	test_observed_ap();
	test_bad_lengths();
	test_encode_and_local();
	printf("PASS: %u shared HE codec checks\n", checks);
	return 0;
}
