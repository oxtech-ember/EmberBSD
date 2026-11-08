/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual-source CCMP replay regression; original harness, AI-assisted. */
#include "ccmp-test-kernel.h"
static unsigned checks, failures;
#define CHECK(c)                                                               \
	do {                                                                   \
		checks++;                                                      \
		if (!(c)) {                                                    \
			failures++;                                            \
			fprintf(stderr, "FAIL line %u: %s\n",                  \
				(unsigned)__LINE__, #c);                       \
		}                                                              \
	} while (0)
static void
mbuf_writable_headroom(void)
{
	struct mbuf m = {0};
	uint8_t external[128];

	m.m_data = m.storage;
	CHECK(M_LEADINGSPACE(&m) == 0);
	m.m_data += 64;
	CHECK(M_LEADINGSPACE(&m) == 64);
	m.m_flags = M_PKTHDR;
	CHECK(M_LEADINGSPACE(&m) == 64);
	/* External-only protection flags do not change internal writability. */
	m.m_flags |= M_EXT_ROMAP;
	CHECK(M_LEADINGSPACE(&m) == 64);
	m.m_flags = M_EXT | M_EXT_RW;
	m.m_ext.ext_buf = external;
	m.m_ext.ext_size = sizeof(external);
	m.m_ext.ext_refcnt = 1;
	m.m_data = external + 32;
	CHECK(M_LEADINGSPACE(&m) == 32);
	m.m_flags |= M_PKTHDR;
	CHECK(M_LEADINGSPACE(&m) == 32);
	m.m_ext.ext_refcnt = 2;
	CHECK(M_LEADINGSPACE(&m) == 0);
	m.m_ext.ext_refcnt = 1;
	m.m_flags |= M_EXT_ROMAP;
	CHECK(M_LEADINGSPACE(&m) == 0);
	m.m_flags = M_EXT;
	CHECK(M_LEADINGSPACE(&m) == 0);
	m.m_flags = M_EXT | M_EXT_RW;
	m.m_data = external;
	CHECK(M_LEADINGSPACE(&m) == 0);
}

static void
init_key(struct ieee80211com *ic, struct ieee80211_key *k, uint64_t rsc,
	 unsigned marker)
{
	memset(k, 0, sizeof(*k));
	k->wk_cipher = &ccmp;
	k->wk_flags = IEEE80211_KEY_SWCRYPT;
	k->wk_keylen = 16;
	k->wk_keyrsc = rsc;
	memset(k->wk_key, (int)marker, 16);
	k->wk_private = ccmp_attach(ic, k);
	assert(k->wk_private != NULL);
	CHECK(ccmp_setkey(k) == 1);
}
static void
frame(struct mbuf *m, struct ieee80211_key *k, int tid, bool addr4, uint64_t pn,
      bool group, unsigned fragment)
{
	int hdr = 24 + (addr4 ? 6 : 0) + (tid >= 0 ? 2 : 0);
	if (((struct ccmp_ctx *)k->wk_private)->cc_ic->ic_flags &
	    IEEE80211_F_DATAPAD)
		hdr = (hdr + 3) & ~3;
	memset(m, 0, sizeof(*m));
	m->m_data = m->storage + 64;
	m->m_flags = M_PKTHDR;
	m->m_len = m->m_pkthdr.len = hdr + 8 + 31;
	m->m_data[0] = (uint8_t)(tid >= 0 ? 0x88 : 0x08);
	m->m_data[1] = (uint8_t)(addr4 ? 3 : 2);
	m->m_data[4] = (uint8_t)(group ? 1 : 2);
	m->m_data[10] = 2;
	m->m_data[16] = 2;
	m->m_data[22] = (uint8_t)(fragment & 15);
	if (fragment & 0x10)
		m->m_data[1] |= 4;
	if (tid >= 0) {
		m->m_data[addr4 ? 30 : 24] = (uint8_t)tid;
	}
	for (int i = 0; i < 31; i++)
		m->m_data[hdr + 8 + i] = (uint8_t)(0x31 + i);
	k->wk_keytsc = pn - 1;
	CHECK(ccmp_encap(k, m, 0) == 1);
}
static struct mbuf *
duplicate(const struct mbuf *in)
{
	struct mbuf *m = malloc(sizeof(*m));
	assert(m);
	memcpy(m, in, sizeof(*m));
	m->m_data = m->storage + (in->m_data - in->storage);
	m->m_next = NULL;
	return m;
}
static int
deliver(struct ieee80211_key *k, const struct mbuf *wire, int hdr)
{
	struct mbuf *m = duplicate(wire);
	int ok = ccmp_decap(k, m, hdr);
	if (ok) {
		uint8_t p[31];
		m_copydata(m, hdr, 31, p);
		for (unsigned i = 0; i < 31; i++)
			CHECK(p[i] == (uint8_t)(0x31 + i));
		CHECK(m->m_pkthdr.len == hdr + 31);
	}
	free(m);
	return ok;
}
static void
replay_domains(void)
{
	struct ifnet ifp = {0};
	struct ieee80211com ic = {.ic_ifp = &ifp};
	struct ieee80211_key rx, tx;
	struct mbuf m;
	init_key(&ic, &rx, 0, 0x11);
	init_key(&ic, &tx, 0, 0x11);
	frame(&m, &tx, -1, false, 100, false, 0);
	CHECK(deliver(&rx, &m, 24) == 1);
	const int tids[] = {0, 7, 15};
	for (unsigned i = 0; i < 3; i++) {
		frame(&m, &tx, tids[i], false, 1, false, 0);
		CHECK(deliver(&rx, &m, 26) == 1);
		CHECK(deliver(&rx, &m, 26) == 0);
	}
	CHECK(replay_event.iev_keyrsc == 1 && replay_event.iev_rsc == 1);
	frame(&m, &tx, 0, false, 2, false, 0);
	CHECK(deliver(&rx, &m, 26) == 1);
	frame(&m, &tx, -1, false, 100, false, 0);
	CHECK(deliver(&rx, &m, 24) == 0);
	CHECK(replay_event.iev_keyrsc == 100);
	/* Authentication failures must not advance the selected replay domain.
	 */
	frame(&m, &tx, 7, false, 1000, false, 0);
	m.m_data[m.m_len - 1] ^= 0x80;
	CHECK(deliver(&rx, &m, 26) == 0);
	frame(&m, &tx, 7, false, 2, false, 0);
	CHECK(deliver(&rx, &m, 26) == 1);
	frame(&m, &tx, 15, true, 2, false, 0);
	CHECK(deliver(&rx, &m, 32) == 1);
	CHECK(deliver(&rx, &m, 32) == 0);
	/* Each fragment consumes a fresh PN; duplicate fragment PN is a replay.
	 */
	frame(&m, &tx, 3, false, 1, false, 0x10);
	CHECK(deliver(&rx, &m, 26) == 1);
	frame(&m, &tx, 3, false, 2, false, 1);
	CHECK(deliver(&rx, &m, 26) == 1);
	CHECK(deliver(&rx, &m, 26) == 0);
	ccmp_detach(&rx);
	ccmp_detach(&tx);
}
static void
key_isolation(void)
{
	struct ieee80211com ic = {0};
	struct ieee80211_key rx[3], tx[3];
	struct mbuf m;
	for (unsigned i = 0; i < 3; i++) {
		init_key(&ic, &rx[i], 0, 0x21 + i);
		init_key(&ic, &tx[i], 0, 0x21 + i);
	}
	for (unsigned i = 0; i < 3; i++) {
		frame(&m, &tx[i], 7, false, 1, i == 2, 0);
		CHECK(deliver(&rx[i], &m, 26) == 1);
		CHECK(deliver(&rx[i], &m, 26) == 0);
	}
	for (unsigned i = 0; i < 3; i++) {
		ccmp_detach(&rx[i]);
		ccmp_detach(&tx[i]);
	}
}
static void
seed_rekey(void)
{
	struct ieee80211com ic = {.ic_opmode = IEEE80211_M_STA};
	struct ieee80211_node ni = {0};
	struct ieee80211req_key req = {0};
	struct ieee80211req ioc = {0};
	struct ieee80211_key tx;
	struct mbuf m;
	ic.ic_bss = &ni;
	req.ik_type = IEEE80211_CIPHER_AES_CCM;
	req.ik_flags = IEEE80211_KEY_XMIT | IEEE80211_KEY_RECV;
	req.ik_keyix = IEEE80211_KEYIX_NONE;
	req.ik_keylen = 16;
	req.ik_keyrsc = 100;
	memset(req.ik_keydata, 0x41, 16);
	ioc.i_len = sizeof(req);
	ioc.i_data = &req;
	CHECK(ieee80211_ioctl_setkey(&ic, &ioc) == 0);
	init_key(&ic, &tx, 0, 0x41);
	for (int tid = -1; tid < 16; tid++) {
		int hdr = tid < 0 ? 24 : 26;
		frame(&m, &tx, tid, false, 100, false, 0);
		CHECK(deliver(&ni.ni_ucastkey, &m, hdr) == 0);
		frame(&m, &tx, tid, false, 101, false, 0);
		CHECK(deliver(&ni.ni_ucastkey, &m, hdr) == 1);
	}
	CHECK(ni.ni_ucastkey.wk_keyrsc == 101);
	req.ik_keyrsc = 0;
	memset(req.ik_keydata, 0x42, 16);
	CHECK(ieee80211_ioctl_setkey(&ic, &ioc) == 0);
	memset(tx.wk_key, 0x42, 16);
	CHECK(ccmp_setkey(&tx) == 1);
	for (int tid = -1; tid < 16; tid++) {
		frame(&m, &tx, tid, false, 1, false, 0);
		CHECK(deliver(&ni.ni_ucastkey, &m, tid < 0 ? 24 : 26) == 1);
	}
	ccmp_detach(&ni.ni_ucastkey);
	ccmp_detach(&tx);
}
static void
malformed_headers(void)
{
	struct ieee80211com ic = {0};
	struct ieee80211_key rx, tx;
	struct mbuf m, *bad;
	init_key(&ic, &rx, 0, 0x51);
	init_key(&ic, &tx, 0, 0x51);
	frame(&m, &tx, 15, true, 1, false, 0);
	const int lengths[] = {-1, 0, 23, 24, 26, 30, 31, 4096, INT_MAX};
	for (unsigned i = 0; i < __arraycount(lengths); i++) {
		bad = duplicate(&m);
		CHECK(ccmp_decap(&rx, bad, lengths[i]) == 0);
		free(bad);
	}
	bad = duplicate(&m);
	bad->m_len = 31;
	CHECK(ccmp_decap(&rx, bad, 32) == 0);
	free(bad);
	bad = duplicate(&m);
	bad->m_pkthdr.len = 32 + 8 + 8;
	CHECK(ccmp_decap(&rx, bad, 32) == 0);
	free(bad);
	bad = duplicate(&m);
	bad->m_len = 1;
	bad->m_pkthdr.len = 1;
	CHECK(ccmp_decap(&rx, bad, 32) == 0);
	free(bad);
	bad = duplicate(&m);
	bad->m_data[32 + 3] &= ~IEEE80211_WEP_EXTIV;
	CHECK(ccmp_decap(&rx, bad, 32) == 0);
	free(bad);
	/* The real generic consumer must reject short four-address QoS IVs. */
	struct ieee80211_node ni = {.ni_ucastkey = rx};
	for (int n = 32; n <= 48; n++) {
		bad = duplicate(&m);
		bad->m_len = bad->m_pkthdr.len = n;
		struct mbuf *input = bad;
		CHECK(ieee80211_crypto_decap(&ic, &ni, &input, 32) == NULL);
		free(bad);
	}
	CHECK(deliver(&rx, &m, 32) == 1);
	/* An invalid key length cannot reset the accepted QoS replay state. */
	rx.wk_keylen = 15;
	CHECK(ccmp_setkey(&rx) == 0);
	rx.wk_keylen = 16;
	CHECK(deliver(&rx, &m, 32) == 0);
	ccmp_detach(&rx);
	ccmp_detach(&tx);
}
static void
generic_key_selection(void)
{
	struct ieee80211com ic = {0};
	struct ieee80211_node ni = {0};
	struct ieee80211_key tx;
	struct mbuf wire, *m;
	init_key(&ic, &ni.ni_ucastkey, 0, 0x61);
	init_key(&ic, &ic.ic_nw_keys[0], 0, 0x61);
	init_key(&ic, &tx, 0, 0x61);
	frame(&wire, &tx, 7, false, 1, false, 0);
	m = duplicate(&wire);
	CHECK(ieee80211_crypto_decap(&ic, &ni, &m, 26) == &ni.ni_ucastkey);
	free(m);
	/* Group and pairwise replay state must remain independent at selection.
	 */
	frame(&wire, &tx, 7, false, 1, true, 0);
	m = duplicate(&wire);
	CHECK(ieee80211_crypto_decap(&ic, &ni, &m, 26) == &ic.ic_nw_keys[0]);
	free(m);
	m = duplicate(&wire);
	CHECK(ieee80211_crypto_decap(&ic, &ni, &m, 26) == NULL);
	free(m);
	/* Chained encrypted payload executes the native CCM mbuf traversal. */
	frame(&wire, &tx, 7, false, 2, false, 0);
	m = duplicate(&wire);
	struct mbuf *tail = calloc(1, sizeof(*tail));
	assert(tail);
	tail->m_data = tail->storage;
	tail->m_len = m->m_len - 41;
	memcpy(tail->m_data, m->m_data + 41, (size_t)tail->m_len);
	m->m_len = 41;
	m->m_next = tail;
	CHECK(ieee80211_crypto_decap(&ic, &ni, &m, 26) == &ni.ni_ucastkey);
	uint8_t payload[31];
	m_copydata(m, 26, 31, payload);
	for (unsigned i = 0; i < 31; i++)
		CHECK(payload[i] == (uint8_t)(0x31 + i));
	free(tail);
	free(m);
	ccmp_detach(&tx);
	ccmp_detach(&ni.ni_ucastkey);
	ccmp_detach(&ic.ic_nw_keys[0]);
}
static void
group_ioctl_seed(void)
{
	struct ieee80211com ic = {0};
	struct ieee80211_node ni = {0};
	struct ieee80211req_key req = {0};
	struct ieee80211req ioc = {0};
	struct ieee80211_key tx;
	struct mbuf wire, *m;

	init_key(&ic, &ni.ni_ucastkey, 0, 0x71);
	init_key(&ic, &ic.ic_nw_keys[0], 0, 0x72);
	req.ik_type = IEEE80211_CIPHER_AES_CCM;
	req.ik_flags = IEEE80211_KEY_XMIT | IEEE80211_KEY_RECV;
	req.ik_keyix = 2;
	req.ik_keylen = 16;
	req.ik_keyrsc = 100;
	memset(req.ik_keydata, 0x73, 16);
	ioc.i_len = sizeof(req);
	ioc.i_data = &req;
	CHECK(ieee80211_ioctl_setkey(&ic, &ioc) == 0);
	init_key(&ic, &tx, 0, 0x73);
	for (int tid = -1; tid < 16; tid++) {
		int hdr = tid < 0 ? 24 : 26;
		frame(&wire, &tx, tid, false, 100, true, 0);
		wire.m_data[hdr + 3] |= 2 << 6;
		m = duplicate(&wire);
		CHECK(ieee80211_crypto_decap(&ic, &ni, &m, hdr) == NULL);
		free(m);
		frame(&wire, &tx, tid, false, 101, true, 0);
		wire.m_data[hdr + 3] |= 2 << 6;
		m = duplicate(&wire);
		CHECK(ieee80211_crypto_decap(&ic, &ni, &m, hdr) ==
		      &ic.ic_nw_keys[2]);
		free(m);
	}
	/* A forged high PN must not raise the GTK2 TID7 replay floor. */
	frame(&wire, &tx, 7, false, 1000, true, 0);
	wire.m_data[29] |= 2 << 6;
	wire.m_data[wire.m_len - 1] ^= 0x80;
	m = duplicate(&wire);
	CHECK(ieee80211_crypto_decap(&ic, &ni, &m, 26) == NULL);
	free(m);
	frame(&wire, &tx, 7, false, 102, true, 0);
	wire.m_data[29] |= 2 << 6;
	m = duplicate(&wire);
	CHECK(ieee80211_crypto_decap(&ic, &ni, &m, 26) == &ic.ic_nw_keys[2]);
	free(m);
	CHECK(ni.ni_ucastkey.wk_keyrsc == 0 && ic.ic_nw_keys[0].wk_keyrsc == 0);
	req.ik_keyrsc = 0;
	memset(req.ik_keydata, 0x74, 16);
	CHECK(ieee80211_ioctl_setkey(&ic, &ioc) == 0);
	memset(tx.wk_key, 0x74, 16);
	CHECK(ccmp_setkey(&tx) == 1);
	for (int tid = -1; tid < 16; tid++) {
		int hdr = tid < 0 ? 24 : 26;
		frame(&wire, &tx, tid, false, 1, true, 0);
		wire.m_data[hdr + 3] |= 2 << 6;
		m = duplicate(&wire);
		CHECK(ieee80211_crypto_decap(&ic, &ni, &m, hdr) ==
		      &ic.ic_nw_keys[2]);
		free(m);
	}
	ccmp_detach(&ni.ni_ucastkey);
	ccmp_detach(&ic.ic_nw_keys[0]);
	ccmp_detach(&ic.ic_nw_keys[2]);
	ccmp_detach(&tx);
}
static void
pn_bounds_and_padding(void)
{
	struct ieee80211com ic = {0};
	struct ieee80211_key rx, tx;
	struct mbuf wire;
	init_key(&ic, &rx, 0, 0x81);
	init_key(&ic, &tx, 0, 0x81);
	frame(&wire, &tx, 15, false, 1, false, 0);
	wire.m_data[26] = 0; /* Invalid received PN0; TX must never emit it. */
	CHECK(deliver(&rx, &wire, 26) == 0);
	frame(&wire, &tx, 15, false, UINT64_C(0xffffffffffff), false, 0);
	CHECK(deliver(&rx, &wire, 26) == 1);
	CHECK(deliver(&rx, &wire, 26) == 0);
	frame(&wire, &tx, 15, false, 1, false, 0);
	wire.m_data[26] = 0;
	CHECK(deliver(&rx, &wire, 26) == 0);
	/* Native data padding occupies bytes after QoS, before the IV. */
	ic.ic_flags = IEEE80211_F_DATAPAD;
	frame(&wire, &tx, 7, false, 1, false, 0);
	CHECK(deliver(&rx, &wire, 28) == 1);
	CHECK(deliver(&rx, &wire, 28) == 0);
	ccmp_detach(&rx);
	ccmp_detach(&tx);
}
/*
 * Hand-built PV0 management headers. Literal CCM answers were constructed
 * independently by ccmp-contract-vectors.py using the SDK OpenSSL primitive.
 * These fail if either the management nonce bit or subtype AAD is omitted.
 */
static void
management_plain(struct mbuf *m, uint8_t subtype, const uint8_t *body, size_t n)
{
	static const uint8_t header[24] = {
		0xd0, 0x40, 0x34, 0x12, 0x02, 0x11, 0x22, 0x33,
		0x44, 0x55, 0x02, 0x22, 0x33, 0x44, 0x55, 0x66,
		0x02, 0x33, 0x44, 0x55, 0x66, 0x77, 0xb0, 0x12
	};
	memset(m, 0, sizeof(*m));
	m->m_data = m->storage + 64;
	m->m_flags = M_PKTHDR;
	m->m_len = m->m_pkthdr.len = (int)(32 + n);
	memcpy(m->m_data, header, sizeof(header));
	m->m_data[0] = subtype;
	memcpy(m->m_data + 32, body, n);
}
static void
management_frame(struct mbuf *m, struct ieee80211_key *k, uint64_t pn)
{
	uint8_t body[31];
	for (unsigned i = 0; i < sizeof(body); i++)
		body[i] = (uint8_t)(0x31 + i);
	management_plain(m, IEEE80211_FC0_SUBTYPE_ACTION, body, sizeof(body));
	k->wk_keytsc = pn - 1;
	CHECK(ccmp_encap(k, m, 0) == 1);
}
static void
management_vectors(void)
{
	static const struct {
		uint8_t subtype;
		unsigned len;
		uint8_t plain[31], encrypted[39];
	} cases[] = {
		{0xc0, 2, {0x06, 0x00},
		 {0x8d, 0x45, 0x9e, 0x13, 0x2a, 0x81, 0xb3, 0x15, 0xe6, 0x6f}},
		{0xa0, 2, {0x08, 0x00},
		 {0x83, 0x45, 0xf4, 0xd9, 0x07, 0xde, 0x8c, 0x29, 0xec, 0x12}},
		{0xd0, 4, {0x08, 0x00, 0xab, 0xcd},
		 {0x83, 0x45, 0x4a, 0xc2, 0xe6, 0x8a, 0xea, 0xc3,
		  0x11, 0xff, 0x77, 0xb0}},
		{0xd0, 31,
		 {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
		  0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40,
		  0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
		  0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f},
		 {0xba, 0x77, 0xd2, 0x3b, 0xb8, 0xbe, 0x31, 0x77,
		  0x0b, 0xfa, 0x7c, 0xe1, 0x7b, 0xf7, 0x29, 0x0c,
		  0xd8, 0x2b, 0x0a, 0x86, 0xda, 0xbc, 0xf9, 0x31,
		  0x7c, 0xc5, 0x3e, 0x48, 0x80, 0x67, 0x53, 0xcb,
		  0xb6, 0x53, 0x8f, 0xf4, 0xa4, 0xcb, 0xa4}}
	};
	static const uint8_t iv[8] = {6, 5, 0, 0x20, 4, 3, 2, 1};
	struct ieee80211com ic = {0};
	struct ieee80211_key tx, rx;
	struct mbuf m, tail;
	uint8_t plain[31], encrypted[39];
	init_key(&ic, &tx, 0, 0);
	init_key(&ic, &rx, 0, 0);
	for (unsigned i = 0; i < 16; i++)
		tx.wk_key[i] = rx.wk_key[i] = (uint8_t)i;
	CHECK(ccmp_setkey(&tx) == 1);
	for (unsigned i = 0; i < __arraycount(cases); i++) {
		management_plain(&m, cases[i].subtype, cases[i].plain,
		    cases[i].len);
		/* The header/IV ends exactly at the first mbuf boundary. */
		memset(&tail, 0, sizeof(tail));
		tail.m_data = tail.storage;
		tail.m_len = m.m_len - 32;
		memcpy(tail.m_data, m.m_data + 32, (size_t)tail.m_len);
		m.m_len = 32;
		m.m_next = &tail;
		tx.wk_keytsc = UINT64_C(0x010203040505);
		CHECK(ccmp_encap(&tx, &m, 0) == 1);
		CHECK(m.m_pkthdr.len == (int)(40 + cases[i].len));
		CHECK(memcmp(m.m_data + 24, iv, sizeof(iv)) == 0);
		m_copydata(&m, 32, (int)cases[i].len + 8, encrypted);
		CHECK(memcmp(encrypted, cases[i].encrypted,
		    cases[i].len + 8) == 0);
		/* RX consumes the literal wire answer, not the TX output. */
		memcpy(tail.m_data, cases[i].encrypted, cases[i].len + 8);
		CHECK(ccmp_setkey(&rx) == 1);
		int accepted = ccmp_decap(&rx, &m, 24);
		CHECK(accepted == 1);
		if (accepted) {
			CHECK(m.m_pkthdr.len == (int)(24 + cases[i].len));
			m_copydata(&m, 24, (int)cases[i].len, plain);
			CHECK(memcmp(plain, cases[i].plain, cases[i].len) == 0);
		}
		CHECK(rx.wk_keyrsc == 0);
	}
	ccmp_detach(&rx);
	ccmp_detach(&tx);
}
static void
ccm_mbuf_boundaries(void)
{
	/* Every layout contains 32 prefix, 31 payload and 16 suffix bytes. */
	static const struct { unsigned count; int len[8]; } layouts[] = {
		{1, {79}}, {3, {32, 31, 16}}, {6, {0, 32, 0, 1, 30, 16}},
		{8, {8, 0, 16, 8, 7, 0, 24, 16}},
		{5, {33, 0, 15, 15, 16}}, {3, {40, 23, 16}}
	};
	struct aesenc aes;
	struct aes_ccm ccm;
	struct mbuf segments[8];
	uint8_t key[16] = {0}, nonce[13] = {0}, aad[22] = {0};
	uint8_t original[79], expected[79], actual[79], tag[8], wiretag[8];
	memset(original, 0xa5, 32);
	for (unsigned i = 0; i < 31; i++)
		original[32 + i] = (uint8_t)i;
	memset(original + 63, 0x5a, 16);
	aes_setenckey128(&aes, key);
	/* Data and management nonce spaces use the same mbuf helper. */
	for (unsigned management = 0; management < 2; management++) {
		nonce[0] = (uint8_t)(management << 4);
		memcpy(expected, original, sizeof(expected));
		aes_ccm_init(&ccm, AES_128_NROUNDS, &aes, 2, 8,
		    nonce, sizeof(nonce), aad, sizeof(aad), 31);
		/* The contiguous core is independent of mbuf offset traversal. */
		aes_ccm_enc(&ccm, expected + 32, expected + 32, 31);
		aes_ccm_tag(&ccm, tag);
		for (unsigned i = 0; i < __arraycount(layouts); i++) {
			size_t offset = 0;
			memset(segments, 0, sizeof(segments));
			for (unsigned j = 0; j < layouts[i].count; j++) {
				segments[j].m_data = segments[j].storage;
				segments[j].m_len = layouts[i].len[j];
				memcpy(segments[j].m_data, original + offset,
				    (size_t)segments[j].m_len);
				offset += (size_t)segments[j].m_len;
				if (j + 1 < layouts[i].count)
					segments[j].m_next = &segments[j + 1];
			}
			CHECK(offset == sizeof(original));
			segments[0].m_pkthdr.len = sizeof(original);
			aes_ccm_init(&ccm, AES_128_NROUNDS, &aes, 2, 8,
			    nonce, sizeof(nonce), aad, sizeof(aad), 31);
			aes_ccm_enc_mbuf(&ccm, segments, 32, 31, wiretag);
			m_copydata(segments, 0, sizeof(actual), actual);
			CHECK(memcmp(expected, actual, sizeof(actual)) == 0);
			CHECK(memcmp(tag, wiretag, sizeof(tag)) == 0);
			aes_ccm_init(&ccm, AES_128_NROUNDS, &aes, 2, 8,
			    nonce, sizeof(nonce), aad, sizeof(aad), 31);
			CHECK(aes_ccm_dec_mbuf(&ccm, segments, 32, 31, tag) == 1);
			m_copydata(segments, 0, sizeof(actual), actual);
			CHECK(memcmp(original, actual, sizeof(actual)) == 0);
			/* On authentication failure only the payload is zeroed. */
			aes_ccm_init(&ccm, AES_128_NROUNDS, &aes, 2, 8,
			    nonce, sizeof(nonce), aad, sizeof(aad), 31);
			aes_ccm_enc_mbuf(&ccm, segments, 32, 31, wiretag);
			wiretag[0] ^= 1;
			aes_ccm_init(&ccm, AES_128_NROUNDS, &aes, 2, 8,
			    nonce, sizeof(nonce), aad, sizeof(aad), 31);
			CHECK(aes_ccm_dec_mbuf(&ccm, segments, 32, 31, wiretag) == 0);
			m_copydata(segments, 0, sizeof(actual), actual);
			CHECK(memcmp(original, actual, 32) == 0);
			CHECK(memcmp(original + 63, actual + 63, 16) == 0);
			for (unsigned j = 32; j < 63; j++)
				CHECK(actual[j] == 0);
		}
	}
}
static void
management_replay(void)
{
	struct ifnet ifp = {0};
	struct ieee80211com ic = {.ic_ifp = &ifp};
	struct ieee80211_key tx, rx;
	struct mbuf wire, *bad;
	init_key(&ic, &tx, 0, 0x91);
	init_key(&ic, &rx, 0, 0x91);
	/* All data domains at PN100 must still admit management PN1. */
	for (int tid = -1; tid < 16; tid++) {
		frame(&wire, &tx, tid, false, 100, false, 0);
		CHECK(deliver(&rx, &wire, tid < 0 ? 24 : 26) == 1);
	}
	management_frame(&wire, &tx, 1);
	CHECK(deliver(&rx, &wire, 24) == 1);
	CHECK(deliver(&rx, &wire, 24) == 0);
	CHECK(replay_event.iev_keyrsc == 1 && replay_event.iev_rsc == 1);
	/* Subtypes differing only in formerly masked bits must authenticate. */
	management_frame(&wire, &tx, 1000);
	bad = duplicate(&wire);
	bad->m_data[0] = IEEE80211_FC0_SUBTYPE_DEAUTH;
	CHECK(ccmp_decap(&rx, bad, 24) == 0);
	free(bad);
	CHECK(rx.wk_keyrsc == 100);
	bad = duplicate(&wire);
	bad->m_data[bad->m_len - 1] ^= 0x80;
	CHECK(ccmp_decap(&rx, bad, 24) == 0);
	free(bad);
	management_frame(&wire, &tx, 2);
	CHECK(deliver(&rx, &wire, 24) == 1);
	management_frame(&wire, &tx, 1001);
	CHECK(deliver(&rx, &wire, 24) == 1);
	/* A high management PN must not advance any data replay floor. */
	for (int tid = -1; tid < 16; tid++) {
		frame(&wire, &tx, tid, false, 101, false, 0);
		CHECK(deliver(&rx, &wire, tid < 0 ? 24 : 26) == 1);
		CHECK(deliver(&rx, &wire, tid < 0 ? 24 : 26) == 0);
	}
	/* The configured floor also seeds management; a rekey reseeds it. */
	rx.wk_keyrsc = 2000;
	CHECK(ccmp_setkey(&rx) == 1);
	management_frame(&wire, &tx, 2000);
	CHECK(deliver(&rx, &wire, 24) == 0);
	management_frame(&wire, &tx, 2001);
	CHECK(deliver(&rx, &wire, 24) == 1);
	rx.wk_keyrsc = 0;
	memset(tx.wk_key, 0x92, 16);
	memset(rx.wk_key, 0x92, 16);
	CHECK(ccmp_setkey(&rx) == 1 && ccmp_setkey(&tx) == 1);
	management_frame(&wire, &tx, 1);
	CHECK(deliver(&rx, &wire, 24) == 1);
	/* Bad key length cannot reset the authenticated management floor. */
	rx.wk_keylen = 15;
	CHECK(ccmp_setkey(&rx) == 0);
	rx.wk_keylen = 16;
	CHECK(deliver(&rx, &wire, 24) == 0);
	ccmp_detach(&rx);
	ccmp_detach(&tx);
}
static void
management_malformed(void)
{
	/* Cases must be format failures before MIC/replay checks. */
	static const struct { unsigned offset; uint8_t mask; } cases[] = {
		{0, 0x01}, /* nonzero protocol version */
		{0, 0x04}, {0, 0x0c}, /* control/reserved frame type */
		{0, 0x40}, /* unsupported management subtype (ATIM) */
		{1, 0x01}, {1, 0x02}, {1, 0x03}, /* DS address formats */
		{1, 0x40}, /* missing Protected bit */
		{1, 0x80}, /* unsupported HT-Control */
		{1, 0x04}, {22, 0x01}, /* unsupported management fragments */
		{4, 0x01}, /* multicast management uses BIP */
		{26, 0x01}, {27, 0x01}, {27, 0x10}, {27, 0x20} /* IV */
	};
	struct ieee80211com ic = {0};
	struct ieee80211_key tx, rx;
	struct mbuf wire, *bad;
	init_key(&ic, &tx, 0, 0xa1);
	init_key(&ic, &rx, 0, 0xa1);
	management_frame(&wire, &tx, 1);
	for (unsigned i = 0; i < __arraycount(cases); i++) {
		unsigned formats = ic.ic_stats.is_rx_ccmpformat;
		bad = duplicate(&wire);
		bad->m_data[cases[i].offset] ^= cases[i].mask;
		CHECK(ccmp_decap(&rx, bad, 24) == 0);
		CHECK(ic.ic_stats.is_rx_ccmpformat == formats + 1);
		free(bad);
		/* TX validates the same management header before changing PN. */
		if (cases[i].offset < 24) {
			uint8_t snapshot[71];
			bad = duplicate(&wire);
			bad->m_data[cases[i].offset] ^= cases[i].mask;
			memcpy(snapshot, bad->m_data, sizeof(snapshot));
			CHECK(ccmp_encap(&tx, bad, 0) == 0);
			CHECK(tx.wk_keytsc == 1);
			CHECK(memcmp(snapshot, bad->m_data, sizeof(snapshot)) == 0);
			free(bad);
		}
	}
	const int lengths[] = {-1, 0, 23, 25, 26, 28, 30, INT_MAX};
	for (unsigned i = 0; i < __arraycount(lengths); i++) {
		bad = duplicate(&wire);
		CHECK(ccmp_decap(&rx, bad, lengths[i]) == 0);
		free(bad);
	}
	/* Test the first-mbuf and packet-length boundaries independently. */
	for (int n = 0; n < 32; n++) {
		bad = duplicate(&wire);
		bad->m_len = n;
		CHECK(ccmp_decap(&rx, bad, 24) == 0);
		CHECK(ccmp_encap(&tx, bad, 0) == 0);
		CHECK(tx.wk_keytsc == 1);
		free(bad);
	}
	for (int n = 0; n <= 40; n++) {
		bad = duplicate(&wire);
		bad->m_pkthdr.len = n;
		CHECK(ccmp_decap(&rx, bad, 24) == 0);
		if (n <= 32) {
			CHECK(ccmp_encap(&tx, bad, 0) == 0);
			CHECK(tx.wk_keytsc == 1);
		}
		free(bad);
	}
	CHECK(deliver(&rx, &wire, 24) == 1);
	ccmp_detach(&rx);
	ccmp_detach(&tx);
}
static void
tx_pn_exhaustion(void)
{
	struct ieee80211com ic = {0};
	struct ieee80211_key tx;
	struct mbuf wire, *next;
	const uint64_t exhausted[] = {UINT64_C(0xffffffffffff),
	    UINT64_C(0x1000000000000), UINT64_MAX};
	init_key(&ic, &tx, 0, 0xb1);
	for (unsigned hardware = 0; hardware < 2; hardware++) {
		for (unsigned management = 0; management < 2; management++) {
			tx.wk_flags = hardware ? 0 : IEEE80211_KEY_SWCRYPT;
			if (management)
				management_frame(&wire, &tx, exhausted[0]);
			else
				frame(&wire, &tx, -1, false, exhausted[0], false, 0);
			CHECK(tx.wk_keytsc == exhausted[0]);
			for (unsigned i = 0; i < __arraycount(exhausted); i++) {
				next = duplicate(&wire);
				tx.wk_keytsc = exhausted[i];
				CHECK(ccmp_encap(&tx, next, 0) == 0);
				CHECK(tx.wk_keytsc == exhausted[i]);
				CHECK(next->m_pkthdr.len == wire.m_pkthdr.len);
				CHECK(memcmp(next->m_data, wire.m_data,
				    (size_t)wire.m_len) == 0);
				free(next);
			}
		}
	}
	ccmp_detach(&tx);
}
static void
native_authenticated_rx(void)
{
    struct ifnet ifp = {0};
    struct ieee80211com ic = {.ic_ifp = &ifp};
    struct ieee80211_key rx, tx;
    struct mbuf m;
    init_key(&ic, &rx, 0, 0x11);
    init_key(&ic, &tx, 0, 0x11);
    frame(&m, &tx, -1, false, 1, false, 0);
    /* Firmware result: authenticated plaintext, retained IV, stripped MIC. */
    for (unsigned i = 0; i < 31; i++) m.m_data[32+i] = 0x31+i;
    m_adj(&m, -8);
    native_rx_status = -1;
    CHECK(deliver(&rx, &m, 24) == 0);
    native_rx_status = 1;
    CHECK(deliver(&rx, &m, 24) == 1);
    CHECK(deliver(&rx, &m, 24) == 0);
    frame(&m, &tx, 7, false, 1, false, 0);
    for (unsigned i = 0; i < 31; i++) m.m_data[34+i] = 0x31+i;
    m_adj(&m, -8);
    m.m_data[28] = 1;
    CHECK(deliver(&rx, &m, 26) == 0);
    m.m_data[28] = 0;
    m.m_data[29] &= ~0x20;
    CHECK(deliver(&rx, &m, 26) == 0);
    m.m_data[29] |= 0x20;
    CHECK(deliver(&rx, &m, 26) == 1);
    frame(&m, &tx, 7, false, 2, false, 0);
    for (unsigned i = 0; i < 31; i++) m.m_data[34+i] = 0x31+i;
    m_adj(&m, -8);
    native_amsdu_index = 1; native_amsdu_last = false;
    CHECK(deliver(&rx, &m, 26) == 1);
    CHECK(deliver(&rx, &m, 26) == 0);
    native_amsdu_index = 3;
    CHECK(deliver(&rx, &m, 26) == 1);
    native_amsdu_index = 2;
    CHECK(deliver(&rx, &m, 26) == 0);
    native_amsdu_index = 4; native_amsdu_epoch++;
    CHECK(deliver(&rx, &m, 26) == 0);
    native_amsdu_epoch--; native_amsdu_last = true;
    CHECK(deliver(&rx, &m, 26) == 1);
    native_amsdu_index = 5;
    CHECK(deliver(&rx, &m, 26) == 0);
    native_amsdu_index = 0;
    management_frame(&m, &tx, 1);
    for (unsigned i = 0; i < 31; i++) m.m_data[32+i] = 0x31+i;
    m_adj(&m, -8);
    CHECK(deliver(&rx, &m, 24) == 1);
    CHECK(deliver(&rx, &m, 24) == 0);
    for (unsigned length = 2; length <= 4; length += 2) {
        uint8_t body[4] = {8, 0, 1, 2};
        management_plain(&m, length == 2 ? 0xc0 : 0xd0, body, length);
        tx.wk_keytsc = length;
        CHECK(ccmp_encap(&tx, &m, 0) == 1);
        memcpy(m.m_data + 32, body, length); m_adj(&m, -8);
        struct mbuf *short_frame = duplicate(&m);
        CHECK(ccmp_decap(&rx, short_frame, 24) == 1);
        CHECK(short_frame->m_pkthdr.len == (int)(24 + length));
        CHECK(memcmp(short_frame->m_data + 24, body, length) == 0);
        free(short_frame);
    }
    frame(&m, &tx, -1, false, 100, true, 0);
    for (unsigned i = 0; i < 31; i++) m.m_data[32+i] = 0x31+i;
    m_adj(&m, -8);
    CHECK(deliver(&rx, &m, 24) == 1);
    CHECK(deliver(&rx, &m, 24) == 0);
    native_rx_status = 0;
    ccmp_detach(&rx); ccmp_detach(&tx);
}
int
main(void)
{
	native_authenticated_rx();
	mbuf_writable_headroom();
	CHECK(aes_ccm_selftest() == 0);
	replay_domains();
	key_isolation();
	seed_rekey();
	malformed_headers();
	generic_key_selection();
	group_ioctl_seed();
	pn_bounds_and_padding();
	ccm_mbuf_boundaries();
	management_vectors();
	management_replay();
	tx_pn_exhaustion();
	management_malformed();
	printf("Native CCMP replay: %u checks, %u failures; RFC3610 AES-CCM "
	       "selftest executed\n",
	       checks, failures);
	return failures != 0;
}
