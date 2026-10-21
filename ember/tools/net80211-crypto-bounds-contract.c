/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual generic crypto RX consumer, with modeled native mbuf services. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#define KASSERT assert
#define IEEE80211_DPRINTF(...) ((void)0)
#define IEEE80211_IS_MULTICAST(a) (*(a) & 1)
#define M_PKTHDR 1
#define IEEE80211_WEP_IVLEN 3
#define IEEE80211_WEP_KIDLEN 1
#define IEEE80211_WEP_HDRLEN 4
struct ieee80211_frame {
	uint8_t fc[2], dur[2], i_addr1[6], addr2[6], addr3[6], seq[2];
};
#define IEEE80211_WEP_MINLEN (sizeof(struct ieee80211_frame) + 4 + 4)
struct mbuf {
	int m_flags, m_len;
	struct {
		int len;
	} m_pkthdr;
	uint8_t data[128];
};
#define mtod(m, t) ((t)(m)->data)
struct ieee80211_key;
struct ieee80211_cipher {
	unsigned ic_header, ic_trailer;
	int (*ic_decap)(struct ieee80211_key *, struct mbuf *, int);
};
struct ieee80211_key {
	const struct ieee80211_cipher *wk_cipher;
};
struct ieee80211_node {
	struct ieee80211_key ni_ucastkey;
};
struct ieee80211com {
	struct {
		unsigned is_rx_tooshort;
	} ic_stats;
	struct ieee80211_key ic_nw_keys[4];
};
static const struct ieee80211_cipher ieee80211_cipher_none = {0};
static unsigned reads, pullups, decaps;
static void
m_copydata(struct mbuf *m, int off, int n, void *out)
{
	/* Fail at the actual mbuf boundary, before modeling any cipher work. */
	assert(off >= 0 && n >= 0 && off <= m->m_pkthdr.len &&
	       n <= m->m_pkthdr.len - off);
	reads++;
	memcpy(out, m->data + off, (size_t)n);
}
static struct mbuf *
m_pullup(struct mbuf *m, int n)
{
	pullups++;
	if (n > m->m_pkthdr.len)
		return NULL;
	static struct mbuf pulled;
	pulled = *m;
	pulled.m_len = pulled.m_pkthdr.len;
	return &pulled;
}
static int
cipher_decap(struct ieee80211_key *k, struct mbuf *m, int hdrlen)
{
	(void)k;
	(void)m;
	(void)hdrlen;
	decaps++;
	return 1;
}
#include "native-crypto-bounds.inc"
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
int
main(void)
{
	const struct ieee80211_cipher cipher = {8, 8, cipher_decap};
	struct ieee80211com ic = {0};
	struct ieee80211_node ni = {{&cipher}};
	struct mbuf m, *mp;
	/* Header lengths cover QoS, addr4 and padded header space. */
	const int headers[] = {24, 26, 28, 30, 32, 36};
	for (unsigned i = 0; i < sizeof(headers) / sizeof(headers[0]); i++) {
		int hdr = headers[i];
		for (int left = 0; left < 4; left++) {
			memset(&m, 0, sizeof(m));
			m.m_flags = M_PKTHDR;
			m.m_len = m.m_pkthdr.len = hdr + left;
			mp = &m;
			unsigned before = reads;
			CHECK(ieee80211_crypto_decap(&ic, &ni, &mp, hdr) ==
			      NULL);
			CHECK(reads == before);
		}
		memset(&m, 0, sizeof(m));
		m.m_flags = M_PKTHDR;
		m.m_pkthdr.len = hdr + 16;
		m.m_len = hdr;
		mp = &m;
		unsigned before = decaps;
		CHECK(ieee80211_crypto_decap(&ic, &ni, &mp, hdr) ==
		      &ni.ni_ucastkey);
		CHECK(decaps == before + 1 && pullups != 0);
	}
	const int invalid[] = {INT_MIN, -1, 0, 23, 65, INT_MAX};
	for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
		memset(&m, 0, sizeof(m));
		m.m_flags = M_PKTHDR;
		m.m_len = m.m_pkthdr.len = 64;
		mp = &m;
		unsigned before = reads;
		CHECK(ieee80211_crypto_decap(&ic, &ni, &mp, invalid[i]) ==
		      NULL);
		CHECK(reads == before);
	}
	printf("Native crypto key-ID bounds: %u checks, %u failures\n", checks,
	       failures);
	return failures != 0;
}
