/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD explicit VHT wire codec contracts, AI-assisted. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <net80211/ieee80211_vht_ies.h>

static unsigned checks;
#define CHECK(e) do { if (!(e)) { \
	fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #e); exit(1); \
} checks++; } while (0)

/* Deliberately asymmetric fields, with raw reserved/highest bits retained. */
static const uint8_t wire[] = {
	191, 12, 0xef, 0xcd, 0xab, 0x89, 0x32, 0x54, 0x76, 0x98,
	0xba, 0xdc, 0xfe, 0xf0, 192, 5, 0xff, 0xe1, 0xd2, 0xc3, 0xb4
};

static void
fields(const struct ieee80211_vht_ies *v)
{
	CHECK(v->cap_present && v->operation_present);
	CHECK(v->cap.info == 0x89abcdef);
	CHECK(v->cap.rx_map == 0x5432);
	CHECK(v->cap.rx_highest == 0x9876);
	CHECK(v->cap.tx_map == 0xdcba);
	CHECK(v->cap.tx_highest == 0xf0fe);
	CHECK(v->operation.width == 0xff);
	CHECK(v->operation.seg0 == 0xe1 && v->operation.seg1 == 0xd2);
	CHECK(v->operation.basic_map == 0xb4c3);
}

static void
clear(const struct ieee80211_vht_ies *v)
{
	const uint8_t *p = (const uint8_t *)v;
	size_t i;

	for (i = 0; i < sizeof(*v); i++)
		CHECK(p[i] == 0);
}

static void
reject(const uint8_t *p, size_t n)
{
	struct ieee80211_vht_ies v;

	memset(&v, 0xa5, sizeof(v));
	CHECK(ieee80211_vht_ies_parse(&v, p, n) == -1);
	clear(&v);
}

static void
decode(void)
{
	struct ieee80211_vht_ies v;
	uint8_t buf[2 * sizeof(wire) + 4], *p;
	size_t n, size, id;

	/* A valid prefix followed by bad framing must publish no partial result. */
	memcpy(buf, wire, sizeof(wire));
	buf[sizeof(wire)] = 1;
	reject(buf, sizeof(wire) + 1);
	CHECK(ieee80211_vht_ies_parse(&v, wire, sizeof(wire)) == 0);
	fields(&v);
	CHECK(ieee80211_vht_ies_parse(&v, NULL, 0) == 0);
	clear(&v);
	reject(NULL, 1);
	CHECK(ieee80211_vht_ies_parse(NULL, wire, sizeof(wire)) == -1);
	for (id = 191; id <= 192; id++) {
		size = id == 191 ? 12 : 5;
		for (n = 0; n <= 255; n++) {
			p = malloc(n + 2);
			CHECK(p != NULL);
			memset(p, 0xff, n + 2);
			p[0] = id;
			p[1] = n;
			if (n == size) {
				CHECK(ieee80211_vht_ies_parse(&v, p, n + 2) == 0);
				CHECK(v.cap_present == (id == 191));
				CHECK(v.operation_present == (id == 192));
			} else
				reject(p, n + 2);
			/* Truncation of even a correctly sized declared payload. */
			p[1] = size;
			if (n < size)
				reject(p, n + 2);
			free(p);
		}
	}
	for (n = 0; n <= sizeof(wire); n++) {
		p = malloc(n != 0 ? n : 1);
		CHECK(p != NULL);
		memcpy(p, wire, n);
		if (n == 0 || n == 14 || n == sizeof(wire))
			CHECK(ieee80211_vht_ies_parse(&v, p, n) == 0);
		else
			reject(p, n);
		free(p);
	}
	/* Both duplicate classes reject in either order, including late failure. */
	memcpy(buf, wire, sizeof(wire));
	memcpy(buf + sizeof(wire), wire, 14);
	reject(buf, sizeof(wire) + 14);
	memcpy(buf + sizeof(wire), wire + 14, 7);
	reject(buf, sizeof(wire) + 7);
	memcpy(buf, wire + 14, 7);
	memcpy(buf + 7, wire, 14);
	CHECK(ieee80211_vht_ies_parse(&v, buf, sizeof(wire)) == 0);
	fields(&v);
	/* Unrelated complete TLVs are skipped, but still require valid framing. */
	buf[0] = 221; buf[1] = 1; buf[2] = 0xff;
	memcpy(buf + 3, wire, sizeof(wire));
	buf[3 + sizeof(wire)] = 0; buf[4 + sizeof(wire)] = 0;
	CHECK(ieee80211_vht_ies_parse(&v, buf, sizeof(wire) + 5) == 0);
	fields(&v);
	CHECK(ieee80211_vht_ies_parse(&v, buf, 3) == 0);
	clear(&v);
	buf[1] = 2;
	reject(buf, 3);
}

static void
encode(void)
{
	struct ieee80211_vht_ies v;
	union { struct ieee80211_vht_cap cap; uint8_t b[32]; } aliascap;
	union { struct ieee80211_vht_operation op; uint8_t b[32]; } aliasop;
	uint8_t b[32], want[32], joined[sizeof(wire)];
	size_t n;

	CHECK(ieee80211_vht_ies_parse(&v, wire, sizeof(wire)) == 0);
	memset(want, 0xa5, sizeof(want));
	for (n = 0; n < 12; n++) {
		memcpy(b, want, sizeof(b));
		CHECK(ieee80211_vht_cap_encode(b + 1, n, &v.cap) == -1);
		CHECK(memcmp(b, want, sizeof(b)) == 0);
	}
	for (n = 0; n < 5; n++) {
		memcpy(b, want, sizeof(b));
		CHECK(ieee80211_vht_operation_encode(b + 1, n, &v.operation) == -1);
		CHECK(memcmp(b, want, sizeof(b)) == 0);
	}
	memcpy(b, want, sizeof(b));
	CHECK(ieee80211_vht_cap_encode(b + 1, 31, NULL) == -1);
	CHECK(memcmp(b, want, sizeof(b)) == 0);
	CHECK(ieee80211_vht_operation_encode(b + 1, 31, NULL) == -1);
	CHECK(memcmp(b, want, sizeof(b)) == 0);
	CHECK(ieee80211_vht_cap_encode(NULL, 12, &v.cap) == -1);
	CHECK(ieee80211_vht_operation_encode(NULL, 5, &v.operation) == -1);
	CHECK(ieee80211_vht_cap_encode(b + 1, 31, &v.cap) == 0);
	memcpy(want + 1, wire + 2, 12);
	CHECK(memcmp(b, want, sizeof(b)) == 0);
	memset(b, 0xa5, sizeof(b)); memset(want, 0xa5, sizeof(want));
	CHECK(ieee80211_vht_operation_encode(b + 1, 31, &v.operation) == 0);
	memcpy(want + 1, wire + 16, 5);
	CHECK(memcmp(b, want, sizeof(b)) == 0);
	memcpy(joined, wire, sizeof(wire));
	CHECK(ieee80211_vht_cap_encode(joined + 2, 12, &v.cap) == 0);
	CHECK(ieee80211_vht_operation_encode(joined + 16, 5, &v.operation) == 0);
	CHECK(memcmp(joined, wire, sizeof(wire)) == 0);
	aliascap.cap = v.cap;
	CHECK(ieee80211_vht_cap_encode(aliascap.b + 1, 12, &aliascap.cap) == 0);
	CHECK(memcmp(aliascap.b + 1, wire + 2, 12) == 0);
	aliasop.op = v.operation;
	CHECK(ieee80211_vht_operation_encode(aliasop.b + 1, 5, &aliasop.op) == 0);
	CHECK(memcmp(aliasop.b + 1, wire + 16, 5) == 0);
	/* Width zero remains a raw observation through both codecs. */
	v.cap.info = 0; v.operation.width = 0;
	CHECK(ieee80211_vht_cap_encode(joined + 2, 12, &v.cap) == 0);
	CHECK(ieee80211_vht_operation_encode(joined + 16, 5, &v.operation) == 0);
	CHECK(ieee80211_vht_ies_parse(&v, joined, sizeof(joined)) == 0);
	CHECK(v.cap.info == 0 && v.operation.width == 0);
}

static void
mcs_intersection(void)
{
	/* Rows/columns encode MCS 0..7, 0..8, 0..9, unsupported. */
	static const unsigned want[4][4] = {
		{0, 0, 0, 3}, {0, 1, 1, 3},
		{0, 1, 2, 3}, {3, 3, 3, 3}
	};
	uint16_t a, b, expected, mask;
	unsigned nss, x, y;

	/* A numeric minimum incorrectly turns unsupported into support. */
	CHECK(ieee80211_vht_mcs_intersect(0xffff, 0) == 0xffff);
	for (nss = 0; nss < 8; nss++) {
		mask = (uint16_t)(3U << (2 * nss));
		for (x = 0; x < 4; x++) {
			for (y = 0; y < 4; y++) {
				a = (uint16_t)(0xffffU & ~mask) |
				    (uint16_t)(x << (2 * nss));
				b = (uint16_t)(0xffffU & ~mask) |
				    (uint16_t)(y << (2 * nss));
				expected = (uint16_t)(0xffffU & ~mask) |
				    (uint16_t)(want[x][y] << (2 * nss));
				CHECK(ieee80211_vht_mcs_intersect(a, b) ==
				    expected);
			}
		}
	}
	/* Unsupported holes do not terminate processing of later streams. */
	CHECK(ieee80211_vht_mcs_intersect(0xe4e4, 0x1b1b) == 0xd7d7);
	CHECK(ieee80211_vht_mcs_intersect(0xbfff, 0x7fff) == 0x7fff);
	CHECK(ieee80211_vht_mcs_intersect(0, 0) == 0);
}

static void
mcs_candidates(void)
{
	struct ieee80211_vht_mcs_maps local = { .tx_map = 0xfffa,
	    .rx_map = 0xfff0 };
	struct ieee80211_vht_mcs_maps peer = { .tx_map = 0xfff5,
	    .rx_map = 0xfff4 };
	struct ieee80211_vht_mcs_maps candidate;

	candidate = ieee80211_vht_mcs_candidates(local, peer);
	CHECK(candidate.tx_map == 0xfff4);
	CHECK(candidate.rx_map == 0xfff0);
	candidate = ieee80211_vht_mcs_candidates(peer, local);
	CHECK(candidate.tx_map == 0xfff0);
	CHECK(candidate.rx_map == 0xfff4);
	local.tx_map = local.rx_map = 0xffff;
	candidate = ieee80211_vht_mcs_candidates(local, peer);
	CHECK(candidate.tx_map == 0xffff && candidate.rx_map == 0xffff);
	local.tx_map = 0xbfff; local.rx_map = 0xe4e4;
	peer.tx_map = 0x1b1b; peer.rx_map = 0x7fff;
	candidate = ieee80211_vht_mcs_candidates(local, peer);
	CHECK(candidate.tx_map == 0x7fff && candidate.rx_map == 0xd7d7);
}

static void
mcs_requirements(void)
{
	/* Rows are support; columns are requirements. 3 requires nothing. */
	static const bool want[4][4] = {
		{true, false, false, true}, {true, true, false, true},
		{true, true, true, true}, {false, false, false, true}
	};
	uint16_t supported, required, mask;
	unsigned nss, x, y;

	CHECK(!ieee80211_vht_mcs_meets(0xfffc, 0xfffd));
	for (nss = 0; nss < 8; nss++) {
		mask = (uint16_t)(3U << (2 * nss));
		for (x = 0; x < 4; x++) {
			for (y = 0; y < 4; y++) {
				supported = (uint16_t)(0xffffU & ~mask) |
				    (uint16_t)(x << (2 * nss));
				required = (uint16_t)(0xffffU & ~mask) |
				    (uint16_t)(y << (2 * nss));
				CHECK(ieee80211_vht_mcs_meets(supported,
				    required) == want[x][y]);
			}
		}
	}
	CHECK(ieee80211_vht_mcs_meets(0xffff, 0xffff));
	CHECK(ieee80211_vht_mcs_meets(0xaaaa, 0));
	CHECK(!ieee80211_vht_mcs_meets(0xeaaa, 0));
	CHECK(!ieee80211_vht_mcs_meets(0x6aaa, 0xbfff));
	CHECK(ieee80211_vht_mcs_meets(0xe4e4, 0xf7f7));
	CHECK(!ieee80211_vht_mcs_meets(0xe4e4, 0xf6f7));
}

/* HT Capabilities, HT Operation, VHT Capabilities and VHT Operation. */
static size_t
phy_window(uint8_t *p, unsigned primary, unsigned sco, unsigned width,
    unsigned seg0, unsigned seg1)
{
	memset(p, 0, 73);
	p[0] = 45; p[1] = 26; p[2] = 0x02;	/* 40 MHz support. */
	p[5] = 0xff;				/* HT MCS0-7. */
	p[28] = 61; p[29] = 22; p[30] = (uint8_t)primary;
	p[31] = (uint8_t)(sco | 0x04);		/* Secondary offset, any width. */
	p[52] = 191; p[53] = 12;
	p[58] = 0xfc; p[59] = 0xff;		/* Peer RX MCS0-7, NSS1. */
	p[62] = 0xfc; p[63] = 0xff;		/* Peer TX MCS0-7, NSS1. */
	p[66] = 192; p[67] = 5; p[68] = (uint8_t)width;
	p[69] = (uint8_t)seg0; p[70] = (uint8_t)seg1;
	p[71] = 0xfc; p[72] = 0xff;		/* Basic MCS0-7, NSS1. */
	return 73;
}

static void
derive_rejects(const uint8_t *p, size_t n, unsigned primary)
{
	struct ieee80211_vht_channel c;

	memset(&c, 0xa5, sizeof(c));
	CHECK(ieee80211_vht_channel_derive(p, n, primary, &c) == -1);
	CHECK(c.primary == 0 && c.center == 0 && c.width_mhz == 0);
}

/*
 * IEEE 802.11ac-2013 Table 8-183x and §10.39.1: width0 is the HT 20/40
 * MHz channel from HT Operation, width1 is 80 MHz at segment0 (also the
 * primary 80 of a 160 or 80+80 BSS signalled through segment1), and the
 * deprecated widths 2 and 3 carry 160 and 80+80 in segment0.
 */
static void
channel_derive(void)
{
	static const struct {
		uint8_t primary, sco, width, seg0, seg1, center, mhz;
	} v[] = {
		{ 36, 1, 1, 42, 0, 42, 80 }, { 40, 3, 1, 42, 0, 42, 80 },
		{ 44, 1, 1, 42, 0, 42, 80 }, { 48, 3, 1, 42, 0, 42, 80 },
		{ 52, 1, 1, 58, 0, 58, 80 }, { 56, 3, 1, 58, 0, 58, 80 },
		{ 60, 1, 1, 58, 0, 58, 80 }, { 64, 3, 1, 58, 0, 58, 80 },
		{ 100, 1, 1, 106, 0, 106, 80 }, { 112, 3, 1, 106, 0, 106, 80 },
		{ 116, 1, 1, 122, 0, 122, 80 }, { 128, 3, 1, 122, 0, 122, 80 },
		{ 132, 1, 1, 138, 0, 138, 80 }, { 144, 3, 1, 138, 0, 138, 80 },
		{ 149, 1, 1, 155, 0, 155, 80 }, { 161, 3, 1, 155, 0, 155, 80 },
		/* 160 MHz (segment1 is the 160 center) and 80+80. */
		{ 64, 3, 1, 58, 50, 58, 80 }, { 36, 1, 1, 42, 50, 42, 80 },
		{ 100, 1, 1, 106, 114, 106, 80 }, { 36, 1, 1, 42, 106, 42, 80 },
		{ 161, 3, 1, 155, 42, 155, 80 },
		/* Deprecated 160 and 80+80 encodings. */
		{ 149, 1, 1, 155, 163, 155, 80 }, { 149, 1, 2, 163, 0, 155, 80 },
		{ 64, 3, 2, 50, 0, 58, 80 }, { 36, 1, 2, 50, 0, 42, 80 },
		{ 116, 1, 2, 114, 0, 122, 80 }, { 36, 1, 3, 42, 106, 42, 80 },
		/* HT 40 MHz, segments reserved. */
		{ 36, 1, 0, 0, 0, 38, 40 }, { 40, 3, 0, 0, 0, 38, 40 },
		{ 60, 1, 0, 0, 0, 62, 40 }, { 64, 3, 0, 0, 0, 62, 40 },
		{ 140, 1, 0, 0, 0, 142, 40 }, { 144, 3, 0, 0, 0, 142, 40 },
		{ 149, 1, 0, 0, 0, 151, 40 }, { 161, 3, 0, 9, 7, 159, 40 }
	};
	static const struct {
		uint8_t primary, sco, width, seg0, seg1;
	} bad[] = {
		{ 36, 3, 1, 42, 0 }, { 40, 1, 1, 42, 0 }, { 64, 1, 1, 58, 0 },
		{ 52, 3, 1, 58, 0 }, { 165, 3, 1, 155, 0 }, { 36, 1, 1, 58, 0 },
		{ 36, 1, 1, 50, 0 }, { 36, 1, 1, 46, 0 }, { 36, 1, 1, 0, 0 },
		{ 36, 1, 1, 42, 46 }, { 36, 1, 1, 42, 58 }, { 36, 1, 1, 42, 34 },
		{ 64, 3, 2, 58, 0 }, { 36, 1, 3, 50, 106 }, { 36, 1, 4, 42, 0 },
		{ 36, 1, 255, 42, 0 }, { 36, 0, 0, 0, 0 }, { 36, 0, 1, 42, 0 },
		{ 36, 2, 1, 42, 0 }, { 165, 1, 0, 0, 0 }, { 144, 1, 0, 0, 0 },
		{ 36, 3, 0, 0, 0 }, { 6, 1, 0, 0, 0 }, { 1, 3, 0, 0, 0 },
		{ 177, 1, 1, 171, 0 },
		/* Off-block secondary 80, 80+80 too close, 160 off the primary. */
		{ 36, 1, 1, 42, 100 }, { 36, 1, 3, 42, 100 }, { 36, 1, 3, 42, 58 },
		{ 100, 1, 2, 50, 0 }, { 36, 1, 1, 58, 50 }, { 36, 3, 1, 42, 50 }
	};
	uint8_t p[128];
	struct ieee80211_vht_channel c;
	size_t n;
	unsigned i;

	for (i = 0; i < sizeof(v) / sizeof(v[0]); i++) {
		n = phy_window(p, v[i].primary, v[i].sco, v[i].width,
		    v[i].seg0, v[i].seg1);
		memset(&c, 0xa5, sizeof(c));
		CHECK(ieee80211_vht_channel_derive(p, n, v[i].primary, &c) == 0);
		CHECK(c.primary == v[i].primary && c.center == v[i].center &&
		    c.width_mhz == v[i].mhz);
	}
	for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		n = phy_window(p, bad[i].primary, bad[i].sco, bad[i].width,
		    bad[i].seg0, bad[i].seg1);
		derive_rejects(p, n, bad[i].primary);
	}
	n = phy_window(p, 64, 3, 1, 58, 0);
	derive_rejects(p, n, 60);		/* Not the current primary. */
	p[31] = 3;				/* No STA channel width. */
	derive_rejects(p, n, 64);
	(void)phy_window(p, 64, 3, 1, 58, 0);
	derive_rejects(p, 66, 64);		/* No VHT Operation. */
	memcpy(p + 28, p + 52, 21);		/* No HT Operation. */
	derive_rejects(p, 49, 64);
	n = phy_window(p, 64, 3, 1, 58, 0);
	memcpy(p + n, p + 28, 24);		/* Duplicate HT Operation. */
	derive_rejects(p, n + 24, 64);
	n = phy_window(p, 64, 3, 1, 58, 0);
	p[29] = 21;				/* Well-framed short HT Operation. */
	memmove(p + 51, p + 52, n - 52);
	derive_rejects(p, n - 1, 64);
	n = phy_window(p, 64, 3, 1, 58, 0);
	p[67] = 6;				/* Truncated VHT Operation. */
	derive_rejects(p, n, 64);
	n = phy_window(p, 64, 3, 1, 58, 0);
	CHECK(ieee80211_vht_channel_derive(p, n, 64, NULL) == -1);
	derive_rejects(NULL, 0, 64);
}

/* The candidate binds the strict local and peer profile to one channel. */
static void
assoc_candidate(void)
{
	static const struct ieee80211_vht_cap local = {
		.info = 0, .rx_map = 0xfffc, .rx_highest = 0,
		.tx_map = 0xfffc, .tx_highest = 0
	};
	static const struct {
		uint8_t primary, sco, width, seg0, center, mhz;
	} v[] = {
		{ 36, 1, 1, 42, 42, 80 }, { 64, 3, 1, 58, 58, 80 },
		{ 64, 3, 0, 0, 62, 40 }, { 149, 1, 0, 0, 151, 40 }
	};
	struct ieee80211_vht_cap other;
	struct ieee80211_vht_channel c, wrong;
	uint8_t p[96];
	size_t n;
	unsigned i, mutation;

	for (i = 0; i < sizeof(v) / sizeof(v[0]); i++) {
		n = phy_window(p, v[i].primary, v[i].sco, v[i].width,
		    v[i].seg0, 0);
		CHECK(ieee80211_vht_channel_derive(p, n, v[i].primary, &c) == 0);
		CHECK(c.center == v[i].center && c.width_mhz == v[i].mhz);
		CHECK(ieee80211_vht_assoc_candidate(&local, p, n, &c) == 0);
		wrong = c;
		wrong.center = (uint8_t)(c.center + 16);
		CHECK(ieee80211_vht_assoc_candidate(&local, p, n, &wrong) == -1);
		wrong = c;
		wrong.width_mhz = c.width_mhz == 80 ? 40 : 80;
		CHECK(ieee80211_vht_assoc_candidate(&local, p, n, &wrong) == -1);
		wrong = c;
		wrong.primary = (uint8_t)(c.primary + 4);
		CHECK(ieee80211_vht_assoc_candidate(&local, p, n, &wrong) == -1);
	}
	for (mutation = 0; mutation < 10; mutation++) {
		n = phy_window(p, 64, 3, 1, 58, 0);
		CHECK(ieee80211_vht_channel_derive(p, n, 64, &c) == 0);
		other = local;
		switch (mutation) {
		case 0: other.info = 1; break;		/* Optional capability. */
		case 1: other.tx_map = 0xfffd; break;	/* Unqualified MCS8. */
		case 2: other.rx_highest = 1; break;
		case 3: p[58] = p[59] = 0xff; break;	/* Peer cannot receive. */
		case 4: p[62] = p[63] = 0xff; break;	/* Peer cannot transmit. */
		case 5: p[0] = 221; break;		/* No HT Capabilities. */
		case 6: p[2] = 0; break;		/* No HT 40 MHz support. */
		case 7: p[5] = 0x7f; break;		/* Incomplete HT MCS0-7. */
		case 8: n = 74; p[73] = 0; break;	/* Oversized window. */
		case 9: p[53] = 11; break;		/* Malformed VHT capability. */
		}
		CHECK(ieee80211_vht_assoc_candidate(&other, p, n, &c) == -1);
	}
	/*
	 * Deliberate deviation from IEEE 802.11ac-2013 §10.39.7: the Basic
	 * VHT-MCS and NSS set is not enforced. The caller offers VHT only
	 * beside a nonempty, supported legacy Basic Rate Set (§9.7.5.3).
	 */
	for (mutation = 0; mutation < 3; mutation++) {
		static const uint16_t basic[] = { 0xfffd, 0xff9a, 0x0000 };

		n = phy_window(p, 64, 3, 1, 58, 0);
		p[71] = (uint8_t)basic[mutation];
		p[72] = (uint8_t)(basic[mutation] >> 8);
		CHECK(ieee80211_vht_channel_derive(p, n, 64, &c) == 0);
		CHECK(ieee80211_vht_assoc_candidate(&local, p, n, &c) == 0);
	}
	n = phy_window(p, 64, 3, 1, 58, 0);
	CHECK(ieee80211_vht_assoc_candidate(&local, p, n, NULL) == -1);
	CHECK(ieee80211_vht_assoc_candidate(NULL, p, n, &c) == -1);
}

/*
 * An observed 5 GHz AP: primary 64 with its secondary below, a 160 MHz
 * BSS (segment0 58, segment1 50) and a four-stream Basic VHT-MCS set
 * 0xff9a (NSS1, NSS2 and NSS4 MCS0-9, NSS3 MCS0-8). The NSS1 MCS0-7 station operates on the primary 80 MHz.
 */
static void
observed_ap(void)
{
	static const uint8_t window[] = {
		45, 0x1a, 0xee, 0x09, 0x17, 0xff, 0xff, 0xff, 0xff, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x18, 0x04, 0x87, 0x19, 0x00, 61, 0x16,
		0x40, 0x07, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 191, 0x0c, 0xf6, 0x99, 0xcb, 0x33, 0x9a, 0xff,
		0x30, 0x0c, 0x9a, 0xff, 0x30, 0x2c, 192, 0x05, 0x01, 0x3a,
		0x32, 0x9a, 0xff
	};
	static const struct ieee80211_vht_cap local = {
		.info = 0, .rx_map = 0xfffc, .rx_highest = 0,
		.tx_map = 0xfffc, .tx_highest = 0
	};
	struct ieee80211_vht_channel c;

	CHECK(sizeof(window) == 73);
	CHECK(ieee80211_vht_channel_derive(window, sizeof(window), 64, &c) == 0);
	CHECK(c.primary == 64 && c.center == 58 && c.width_mhz == 80);
	CHECK(ieee80211_vht_assoc_candidate(&local, window, sizeof(window),
	    &c) == 0);
}

int
main(int argc, char **argv)
{
	if (argc == 1 || strcmp(argv[1], "decode") == 0)
		decode();
	if (argc == 1 || strcmp(argv[1], "fields") == 0) {
		struct ieee80211_vht_ies v;
		CHECK(ieee80211_vht_ies_parse(&v, wire, sizeof(wire)) == 0);
		fields(&v);
	}
	if (argc == 1 || strcmp(argv[1], "encode") == 0)
		encode();
	if (argc == 1 || strcmp(argv[1], "mcs-intersection") == 0)
		mcs_intersection();
	if (argc == 1 || strcmp(argv[1], "mcs-candidates") == 0)
		mcs_candidates();
	if (argc == 1 || strcmp(argv[1], "mcs-requirements") == 0)
		mcs_requirements();
	if (argc == 1 || strcmp(argv[1], "channel-derive") == 0)
		channel_derive();
	if (argc == 1 || strcmp(argv[1], "assoc-candidate") == 0)
		assoc_candidate();
	if (argc == 1 || strcmp(argv[1], "observed-ap") == 0)
		observed_ap();
	printf("PASS: %u shared VHT codec and MCS checks\n", checks);
	return 0;
}
