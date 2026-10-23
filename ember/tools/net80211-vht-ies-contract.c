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
	printf("PASS: %u shared VHT codec and MCS checks\n", checks);
	return 0;
}
