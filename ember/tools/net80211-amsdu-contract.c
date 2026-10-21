/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual input/decap functions; mbuf, crypto and delivery boundaries modeled.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <arpa/inet.h>
#define __packed __attribute__((__packed__))
#define le16toh(x) (x)
#include "ieee80211.h"
#if __has_include("ieee80211_amsdu.h")
#include "ieee80211_amsdu.h"
#endif
#define IEEE80211_NODE_AMSDU_RX 0x0040
#define IEEE80211_M_STA 1
#define IEEE80211_M_IBSS 0
#define IEEE80211_M_AHDEMO 3
#define IEEE80211_M_HOSTAP 6
#define IEEE80211_NO_HOSTAP 1
#define IEEE80211_F_PRIVACY 1
#define IEEE80211_F_DROPUNENC 2
#define IEEE80211_CIPHER_AES_CCM 3
#define IFF_SIMPLEX 1
#define ETHERTYPE_PAE 0x888e
#define LLC_SNAP_LSAP 0xaa
#define LLC_UI 3
#define M_DONTWAIT 0
#define M_NOWAIT 0
#define MT_DATA 0
#define M_EXT 1
#define M_PKTHDR 2
#define MHLEN 200
#define IEEE80211_DISCARD(...) ((void)0)
#define IEEE80211_DISCARD_MAC(...) ((void)0)
#define IEEE80211_NODE_STAT(n, s) ((n)->stats.s++)
#define IEEE80211_NODE_STAT_ADD(n, s, v) ((n)->stats.s += (v))
#define IEEE80211_ADDR_EQ(a, b) (memcmp(a, b, 6) == 0)
#define IEEE80211_ADDR_COPY(a, b) memcpy(a, b, 6)
#define mtod(m, t) ((t)((m)->m_data))
#define ETHER_ALIGN 2
#define MGETHDR(m, w, t) ((m) = newbuf())
#define MCLGET(m, w)                                                           \
	do {                                                                   \
		if (++clusters != cluster_fail_at)                             \
			(m)->m_flags |= M_EXT;                                 \
	} while (0)
#define if_statinc(i, s) ((i)->s++)
#define KASSERT(x) assert(x)
struct ether_header {
	uint8_t ether_dhost[6], ether_shost[6];
	uint16_t ether_type;
} __packed;
struct llc {
	uint8_t llc_dsap, llc_ssap, llc_control;
	struct {
		uint8_t org_code[3];
		/* Keep the wire layout used by native decap pointer movement. */
		// cppcheck-suppress unusedStructMember
		uint16_t ether_type;
	} __packed llc_snap;
} __packed;
struct mbuf {
	uint8_t data[4096];
	int off, m_len, m_flags;
	uint8_t *m_data;
	struct {
		int len;
		uint16_t rcvif_index;
	} m_pkthdr;
	struct mbuf *m_nextpkt;
};
struct ifnet {
	int if_flags, if_ipackets, if_ierrors;
};
struct ieee80211_cipher {
	int ic_cipher;
};
struct ieee80211_key {
	const struct ieee80211_cipher *wk_cipher;
};
struct ieee80211_node {
	/* Native debug macros use this field; debug output is disabled here. */
	// cppcheck-suppress unusedStructMember
	uint8_t ni_macaddr[6];
	unsigned ni_flags;
	int auth;
	struct {
		int rx_noprivacy, rx_wepfail, rx_demicfail, rx_decap, rx_unauth,
		    rx_unencrypted, rx_data, rx_bytes;
	} stats;
};
struct ieee80211com {
	struct ifnet *ic_ifp;
	int ic_opmode, ic_flags;
	uint8_t ic_myaddr[6];
	void *ic_rawbpf;
	struct {
		int is_rx_tooshort, is_rx_wrongdir, is_rx_mcastecho,
		    is_rx_noprivacy, is_rx_decap, is_rx_unauth,
		    is_rx_unencrypted;
	} ic_stats;
};
static int alive, allocated, fail_at, delivered, crypto_ok = 1, mic_ok = 1,
						 bpf_count;
static int clusters, cluster_fail_at, defrag_calls, checks, need_auth,
    auth_ready;
static uint8_t payload[174][1600];
static size_t payload_len[174];
static struct ieee80211_cipher cipher = {3};
static struct ieee80211_key testkey = {&cipher};
static struct mbuf *
newbuf(void)
{
	struct mbuf *m;
	if (++allocated == fail_at)
		return NULL;
	m = calloc(1, sizeof(*m));
	assert(m);
	alive++;
	m->m_data = m->data;
	m->m_flags = M_PKTHDR;
	return m;
}
static void
m_freem(struct mbuf *m)
{
	if (m) {
		alive--;
		free(m);
	}
}
static struct mbuf *
m_pullup(struct mbuf *m, int n)
{
	if (m->m_len < n) {
		m_freem(m);
		return NULL;
	}
	return m;
}
static void
m_adj(struct mbuf *m, int n)
{
	assert(n >= 0 && n <= m->m_len);
	m->off += n;
	m->m_data += n;
	m->m_len -= n;
	m->m_pkthdr.len -= n;
}
static void
m_copydata(struct mbuf *m, int off, int n, void *p)
{
	assert(off >= 0 && n >= 0 && off + n <= m->m_pkthdr.len);
	assert(!need_auth || auth_ready);
	memcpy(p, mtod(m, uint8_t *) + off, (size_t)n);
}
static int
ieee80211_hdrspace(struct ieee80211com *ic, const void *p)
{
	const uint8_t *f = p;
	(void)ic;
	return (f[0] & 0x80) ? 26 : 24;
}
static struct ieee80211_key *
ieee80211_crypto_decap(struct ieee80211com *ic, struct ieee80211_node *ni,
		       struct mbuf **m, int h)
{
	(void)ic;
	(void)ni;
	(void)m;
	(void)h;
	return crypto_ok ? &testkey : NULL;
}
static int
ieee80211_crypto_demic(struct ieee80211com *ic, struct ieee80211_key *key,
		       struct mbuf *m, int x)
{
	(void)ic;
	(void)key;
	(void)m;
	(void)x;
	auth_ready = mic_ok;
	return mic_ok;
}
static struct mbuf *
ieee80211_defrag(struct ieee80211_node *ni, struct mbuf *m, int h)
{
	(void)ni;
	(void)h;
	defrag_calls++;
	return m;
}
static void
bpf_mtap3(void *p, struct mbuf *m, int dir)
{
	(void)p;
	(void)m;
	(void)dir;
	bpf_count++;
}
#define BPF_D_IN 1
static int
ieee80211_node_is_authorized(const struct ieee80211_node *ni)
{
	return ni->auth;
}
static void
ieee80211_deliver_data(struct ieee80211com *ic, struct ieee80211_node *ni,
		       struct mbuf *m)
{
	(void)ic;
	(void)ni;
	assert(delivered < 174);
	assert(m->m_pkthdr.rcvif_index == 37);
	payload_len[delivered] = (size_t)m->m_pkthdr.len;
	memcpy(payload[delivered], mtod(m, void *), payload_len[delivered]);
	delivered++;
	m_freem(m);
}
#include "native.inc"
static const uint8_t station[6] = {2, 1, 2, 3, 4, 5};
static const uint8_t sender[6] = {2, 9, 8, 7, 6, 5};
static struct ifnet net;
static struct ieee80211com test_ic;
static struct ieee80211_node test_node;
static void
reset(void)
{
	assert(alive == 0);
	memset(&net, 0, sizeof(net));
	memset(&test_ic, 0, sizeof(test_ic));
	memset(&test_node, 0, sizeof(test_node));
	test_ic.ic_ifp = &net;
	test_ic.ic_opmode = IEEE80211_M_STA;
	memcpy(test_ic.ic_myaddr, station, 6);
	test_node.auth = 1;
	test_node.ni_flags = IEEE80211_NODE_AMSDU_RX;
	delivered = allocated = fail_at = bpf_count = clusters =
	    cluster_fail_at = defrag_calls = 0;
	crypto_ok = mic_ok = 1;
	cipher.ic_cipher = 3;
}
static struct mbuf *
frame(void)
{
	struct mbuf *m = newbuf();
	uint8_t *p = m->data;
	m->m_pkthdr.rcvif_index = 37;
	p[0] = 0x88;
	p[1] = 2;
	memcpy(p + 4, station, 6);
	memcpy(p + 10, sender, 6);
	memcpy(p + 16, sender, 6);
	p[24] = 0x80;
	m->m_len = m->m_pkthdr.len = 26;
	return m;
}
static void
subframe(struct mbuf *m, uint8_t value, int pad)
{
	uint8_t *p = m->data + m->m_len;
	memcpy(p, station, 6);
	memcpy(p + 6, sender, 6);
	p[12] = 0;
	p[13] = 9;
	memcpy(p + 14, "\xaa\xaa\x03\0\0\0\x08\0", 8);
	p[22] = value;
	m->m_len += 23;
	if (pad)
		m->data[m->m_len++] = 0x7f;
	m->m_pkthdr.len = m->m_len;
}
static int
input(struct mbuf *m)
{
	int r;
	need_auth = (m->data[1] & 0x40) != 0;
	auth_ready = 0;
	r = ieee80211_input_data(&test_ic, &m, &test_node);
	m_freem(m);
	assert(alive == 0);
	return r;
}

static struct mbuf *
aggregate(void)
{
	struct mbuf *m = frame();
	subframe(m, 0x42, 1);
	subframe(m, 0x99, 0);
	return m;
}

static struct mbuf *
large_aggregate(void)
{
	struct mbuf *m = frame();
	uint8_t h[22];

	subframe(m, 0x42, 0);
	memcpy(h, m->data + 26, sizeof(h));
	h[12] = 5;
	h[13] = 0xe4; /* 1508 bytes of MSDU. */
	memcpy(m->data + 26, h, sizeof(h));
	memcpy(m->data + 26 + 1524, h, sizeof(h));
	m->m_len = m->m_pkthdr.len = 26 + 1524 + 1522;
	return m;
}

#define CHECK(c)                                                               \
	do {                                                                   \
		int passed = (c);                                              \
		checks++;                                                      \
		assert(passed);                                                \
	} while (0)

int
main(void)
{
	struct mbuf *m;
	int i;

	reset();
	m = aggregate();
	CHECK(input(m) == 0);
	CHECK(delivered == 2 && net.if_ipackets == 2);
	CHECK(payload_len[0] == 15 && payload_len[1] == 15);
	CHECK(payload[0][14] == 0x42 && payload[1][14] == 0x99);
	CHECK(memcmp(payload[0], station, 6) == 0);
	CHECK(memcmp(payload[0] + 6, sender, 6) == 0);
	CHECK(payload[0][12] == 8 && payload[0][13] == 0);
	CHECK(bpf_count == 1);
	CHECK(test_node.stats.rx_data == 2 && test_node.stats.rx_bytes == 30);

	reset();
	m = aggregate();
	test_node.ni_flags = 0;
	CHECK(input(m) == -1 && delivered == 0);
	CHECK(test_ic.ic_stats.is_rx_decap == 1 && net.if_ierrors == 1);
	reset();
	m = aggregate();
	m->data[26 + 24 + 13] = 0xff;
	CHECK(input(m) == -1 && delivered == 0 && allocated == 1);
	reset();
	m = aggregate();
	m->data[26 + 24 + 6] = 3;
	CHECK(input(m) == -1 && delivered == 0 && allocated == 1);
	reset();
	m = aggregate();
	m->m_len--;
	m->m_pkthdr.len--;
	CHECK(input(m) == -1 && delivered == 0 && allocated == 1);
	reset();
	m = aggregate();
	m->data[m->m_len++] = 0;
	m->m_pkthdr.len++;
	CHECK(input(m) == -1 && delivered == 0);
	reset();
	m = aggregate();
	memcpy(m->data + 26, "\xaa\xaa\x03\0\0\0", 6);
	CHECK(input(m) == -1 && delivered == 0);
	reset();
	m = aggregate();
	m->data[26 + 20] = 0x88;
	m->data[26 + 21] = 0x8e;
	CHECK(input(m) == -1 && delivered == 0);

	reset();
	m = aggregate();
	memcpy(m->data + 26 + 6, station, 6);
	CHECK(input(m) == -1 && delivered == 0 && allocated == 1);

	for (i = 2; i <= 3; i++) {
		reset();
		m = aggregate();
		fail_at = i;
		CHECK(input(m) == -1 && delivered == 0 && alive == 0);
	}
	for (i = 1; i <= 2; i++) {
		reset();
		m = large_aggregate();
		cluster_fail_at = i;
		CHECK(input(m) == -1 && delivered == 0 && alive == 0);
	}
	reset();
	m = large_aggregate();
	CHECK(input(m) == 0 && delivered == 2 && clusters == 2);
	CHECK(payload_len[0] == 1514 && payload_len[1] == 1514);

	reset();
	m = aggregate();
	test_node.auth = 0;
	CHECK(input(m) == 0 && delivered == 0 &&
	      test_ic.ic_stats.is_rx_unauth == 2);
	CHECK(net.if_ierrors == 2 && net.if_ipackets == 0);
	CHECK(test_node.stats.rx_unauth == 2);
	reset();
	m = aggregate();
	test_ic.ic_flags = IEEE80211_F_DROPUNENC;
	CHECK(input(m) == 0 && delivered == 0 &&
	      test_ic.ic_stats.is_rx_unencrypted == 2);
	reset();
	m = aggregate();
	test_ic.ic_flags = IEEE80211_F_PRIVACY | IEEE80211_F_DROPUNENC;
	m->data[1] |= 0x40;
	CHECK(input(m) == 0 && delivered == 2 && net.if_ipackets == 2);
	reset();
	m = aggregate();
	test_ic.ic_flags = IEEE80211_F_PRIVACY;
	m->data[1] |= 0x40;
	crypto_ok = 0;
	CHECK(input(m) == -1 && delivered == 0 && bpf_count == 0);
	reset();
	m = aggregate();
	test_ic.ic_flags = IEEE80211_F_PRIVACY;
	m->data[1] |= 0x40;
	mic_ok = 0;
	CHECK(input(m) == -1 && delivered == 0 && bpf_count == 0);
	reset();
	m = aggregate();
	test_ic.ic_flags = IEEE80211_F_PRIVACY;
	m->data[1] |= 0x40;
	cipher.ic_cipher = 1;
	CHECK(input(m) == -1 && delivered == 0);
	reset();
	m = aggregate();
	m->data[1] |= 0x40;
	CHECK(input(m) == -1 && delivered == 0 &&
	      test_ic.ic_stats.is_rx_noprivacy == 1);

	reset();
	m = aggregate();
	m->data[1] |= 4;
	CHECK(input(m) == -1 && delivered == 0 && defrag_calls == 0);
	reset();
	m = aggregate();
	m->data[22] = 1;
	CHECK(input(m) == -1 && delivered == 0 && defrag_calls == 0);
	reset();
	m = aggregate();
	m->data[1] |= 0x80;
	CHECK(input(m) == -1 && delivered == 0);
	reset();
	m = aggregate();
	m->data[0] = 0xc8;
	CHECK(input(m) == -1 && delivered == 0);
	reset();
	m = aggregate();
	m->data[4] = 4;
	CHECK(input(m) == -1 && delivered == 0);
	reset();
	m = aggregate();
	m->data[4] = 3;
	CHECK(input(m) == -1 && delivered == 0);
	reset();
	m = aggregate();
	m->data[1] = 3;
	CHECK(input(m) == -1 && delivered == 0);
	reset();
	m = aggregate();
	test_ic.ic_opmode = IEEE80211_M_IBSS;
	CHECK(input(m) == -1 && delivered == 0);
	reset();
	m = aggregate();
	m->m_len = m->m_pkthdr.len = 25;
	CHECK(input(m) == -1 && delivered == 0);

	/* Legacy nonaggregate SNAP retains the exact port/privacy policy. */
	reset();
	m = frame();
	m->data[24] = 0;
	memcpy(m->data + 26, "\xaa\xaa\x03\0\0\0\x08\0\x42", 9);
	m->m_len = m->m_pkthdr.len = 35;
	CHECK(input(m) == 0 && delivered == 1 && payload[0][14] == 0x42);
	reset();
	m = frame();
	m->data[24] = 0;
	test_node.auth = 0;
	memcpy(m->data + 26, "\xaa\xaa\x03\0\0\0\x88\x8e\x42", 9);
	m->m_len = m->m_pkthdr.len = 35;
	CHECK(input(m) == 0 && delivered == 1 && net.if_ipackets == 1);
	reset();
	m = frame();
	m->data[24] = 0;
	test_node.auth = 0;
	memcpy(m->data + 26, "\xaa\xaa\x03\0\0\0\x08\0\x42", 9);
	m->m_len = m->m_pkthdr.len = 35;
	CHECK(input(m) == -1 && delivered == 0 &&
	      test_ic.ic_stats.is_rx_unauth == 1);
	printf("native A-MSDU consumer: %d checks PASS; modeled mbuf/crypto "
	       "services\n",
	       checks);
	return 0;
}
