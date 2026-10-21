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
	frame(&wire, &tx, 15, false, 0, false, 0);
	CHECK(deliver(&rx, &wire, 26) == 0);
	frame(&wire, &tx, 15, false, UINT64_C(0xffffffffffff), false, 0);
	CHECK(deliver(&rx, &wire, 26) == 1);
	CHECK(deliver(&rx, &wire, 26) == 0);
	frame(&wire, &tx, 15, false, 0, false, 0);
	CHECK(deliver(&rx, &wire, 26) == 0);
	/* Native data padding occupies bytes after QoS, before the IV. */
	ic.ic_flags = IEEE80211_F_DATAPAD;
	frame(&wire, &tx, 7, false, 1, false, 0);
	CHECK(deliver(&rx, &wire, 28) == 1);
	CHECK(deliver(&rx, &wire, 28) == 0);
	ccmp_detach(&rx);
	ccmp_detach(&tx);
}
int
main(void)
{
	CHECK(aes_ccm_selftest() == 0);
	replay_domains();
	key_isolation();
	seed_rekey();
	malformed_headers();
	generic_key_selection();
	group_ioctl_seed();
	pn_bounds_and_padding();
	printf("Native CCMP replay: %u checks, %u failures; RFC3610 AES-CCM "
	       "selftest executed\n",
	       checks, failures);
	return failures != 0;
}
