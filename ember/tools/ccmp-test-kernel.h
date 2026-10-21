/* SPDX-License-Identifier: BSD-2-Clause */
/* Modeled kernel services for actual CCMP/AES sources, AI-assisted. */
#ifndef CCMP_TEST_KERNEL_H
#define CCMP_TEST_KERNEL_H
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#define __KERNEL_RCSID(a, b)
#ifndef __packed
#define __packed __attribute__((packed))
#endif
#ifndef __aligned
#define __aligned(n) __attribute__((aligned(n)))
#endif
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#define __BIT(n) (UINT64_C(1) << (n))
#define __BITS(h, l) ((__BIT(h) - 1 + __BIT(h)) & ~(__BIT(l) - 1))
#define __SHIFTIN(v, m) (((uint64_t)(v) << __builtin_ctzll(m)) & (m))
#define KASSERT assert
#define KASSERTMSG(c, ...) assert(c)
#define CTASSERT(c) _Static_assert(c, #c)
#define IASSERT(c, args) assert(c)
#define panic(...) abort()
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#ifndef NBBY
#define NBBY 8
#endif
#define __GNUC_PREREQ__(a, b)                                                  \
	(__GNUC__ > (a) || (__GNUC__ == (a) && __GNUC_MINOR__ >= (b)))
#ifndef le16toh
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define le16toh(n) (n)
#else
#define le16toh(n) __builtin_bswap16(n)
#endif
#endif
#define KM_NOSLEEP 0
#define IFNAMSIZ 16
#define IEEE80211_KEYBUF_SIZE 16
#define IEEE80211_MICBUF_SIZE 16
#define IEEE80211_KEYIX_NONE UINT16_MAX
#define IEEE80211_CIPHER_AES_CCM 3
#define IEEE80211_M_STA 1
#define IEEE80211_F_DATAPAD 0x00002000
#define IEEE80211_DPRINTF(...) ((void)0)
#define IEEE80211_MSG_CRYPTO 0
#define IEEE80211_ADDR_COPY(d, s) memcpy((d), (s), 6)
#define IEEE80211_ADDR_EQ(a, b) (memcmp((a), (b), 6) == 0)
#define RTM_IEEE80211_REPLAY 104
#define M_NOWAIT 0
#define M_PKTHDR 1
#define explicit_memset test_explicit_memset
static void *
test_explicit_memset(void *p, int c, size_t n)
{
	volatile unsigned char *q = p;
	for (size_t i = 0; i < n; i++)
		q[i] = (unsigned char)c;
	return p;
}
static int
consttime_memequal(const void *a, const void *b, size_t n)
{
	const uint8_t *x = a, *y = b;
	unsigned d = 0;
	for (size_t i = 0; i < n; i++)
		d |= x[i] ^ y[i];
	return d == 0;
}
static uint32_t
le32dec(const void *v)
{
	const uint8_t *p = v;
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
	       (uint32_t)p[3] << 24;
}
static uint32_t
be32dec(const void *v)
{
	const uint8_t *p = v;
	return (uint32_t)p[3] | (uint32_t)p[2] << 8 | (uint32_t)p[1] << 16 |
	       (uint32_t)p[0] << 24;
}
static void
le32enc(void *v, uint32_t n)
{
	uint8_t *p = v;
	for (unsigned i = 0; i < 4; i++)
		p[i] = (uint8_t)(n >> (8 * i));
}
static void
be32enc(void *v, uint32_t n)
{
	uint8_t *p = v;
	for (unsigned i = 0; i < 4; i++)
		p[3 - i] = (uint8_t)(n >> (8 * i));
}
#define bswap32 __builtin_bswap32
static void
hexdump(int (*print)(const char *, ...), const char *name, const void *p,
	size_t n)
{
	(void)p;
	print("%s differs (%zu bytes)\n", name, n);
}
#include "aes.h"
#include "aes_bear.h"
#include "aes_ccm.h"
#include "aes_impl.h"
#include "native-80211.inc"
struct mbuf {
	struct mbuf *m_next;
	uint8_t *m_data;
	int m_len, m_flags;
	struct {
		int len;
	} m_pkthdr;
	uint8_t storage[4096];
};
#define mtod(m, t) ((t)((m)->m_data))
struct ieee80211com;
struct ieee80211_key;
typedef uint16_t ieee80211_keyix;
#include "native-key.inc"
struct ifnet {
	unsigned unused;
};
struct ieee80211_node {
	struct ieee80211_key ni_ucastkey;
	uint8_t ni_bssid[6], ni_macaddr[6];
};
struct ieee80211com {
	struct ifnet *ic_ifp;
	struct {
		unsigned is_rx_tooshort, is_crypto_nomem, is_crypto_ccmp,
		    is_rx_ccmpformat, is_rx_ccmpreplay, is_rx_ccmpmic;
	} ic_stats;
	struct ieee80211_node *ic_bss;
	struct ieee80211_key ic_nw_keys[4];
	int ic_opmode, ic_sta, ic_def_txkey, ic_flags;
};
#include "native-request.inc"
static struct ieee80211_replay_event replay_event;
static unsigned replay_events;
static void
rt_ieee80211msg(struct ifnet *ifp, int type, const void *p, size_t n)
{
	(void)ifp;
	assert(type == RTM_IEEE80211_REPLAY && n == sizeof(replay_event));
	memcpy(&replay_event, p, n);
	replay_events++;
}
#include "native-hdrsize.inc"
static int
ieee80211_hdrspace(struct ieee80211com *ic, const void *p)
{
	int n = ieee80211_hdrsize(p);
	return (ic->ic_flags & IEEE80211_F_DATAPAD) ? ((n + 3) & ~3) : n;
}
static void *
kmem_intr_zalloc(size_t n, int flag)
{
	(void)flag;
	void *p = calloc(1, n);
	assert(p);
	return p;
}
static void
kmem_intr_free(void *p, size_t n)
{
	(void)n;
	free(p);
}
static void
m_copydata(struct mbuf *m, int off, int len, void *dest)
{
	uint8_t *p = dest;
	assert(off >= 0 && len >= 0);
	while (m && off >= m->m_len) {
		off -= m->m_len;
		m = m->m_next;
	}
	while (len) {
		assert(m);
		int n = MIN(len, m->m_len - off);
		memcpy(p, m->m_data + off, (size_t)n);
		p += n;
		len -= n;
		off = 0;
		m = m->m_next;
	}
}
static int
m_append(struct mbuf *m, unsigned n, const void *data)
{
	struct mbuf *tail = m;
	while (tail->m_next)
		tail = tail->m_next;
	assert(tail->m_data + tail->m_len + n <=
	       tail->storage + sizeof(tail->storage));
	memcpy(tail->m_data + tail->m_len, data, n);
	tail->m_len += (int)n;
	m->m_pkthdr.len += (int)n;
	return 1;
}
static void
m_adj(struct mbuf *m, int n)
{
	struct mbuf *root = m;
	if (n >= 0) {
		assert(n <= m->m_pkthdr.len);
		root->m_pkthdr.len -= n;
		while (n) {
			assert(m);
			int k = MIN(n, m->m_len);
			m->m_data += k;
			m->m_len -= k;
			n -= k;
			m = m->m_next;
		}
	} else {
		int keep = m->m_pkthdr.len + n;
		assert(keep >= 0);
		root->m_pkthdr.len = keep;
		while (m) {
			if (keep < m->m_len) {
				m->m_len = keep;
				keep = 0;
			} else
				keep -= m->m_len;
			m = m->m_next;
		}
	}
}
#include "aes_ct.c.inc"
#include "aes_ct_enc.c.inc"
#include "native-aes.inc"
uint32_t
aes_setenckey128(struct aesenc *e, const uint8_t k[static 16])
{
	aesbear_setenckey(e, k, 10);
	return 10;
}
void
aes_enc(const struct aesenc *e, const uint8_t in[static 16],
	uint8_t out[static 16], uint32_t nr)
{
	aesbear_enc(e, in, out, nr);
}
void
aes_cbcmac_update1(const struct aesenc *e, const uint8_t in[static 16],
		   size_t n, uint8_t a[static 16], uint32_t nr)
{
	aesbear_cbcmac_update1(e, in, n, a, nr);
}
void
aes_ccm_enc1(const struct aesenc *e, const uint8_t in[static 16],
	     uint8_t out[static 16], size_t n, uint8_t a[static 32],
	     uint32_t nr)
{
	aesbear_ccm_enc1(e, in, out, n, a, nr);
}
void
aes_ccm_dec1(const struct aesenc *e, const uint8_t in[static 16],
	     uint8_t out[static 16], size_t n, uint8_t a[static 32],
	     uint32_t nr)
{
	aesbear_ccm_dec1(e, in, out, n, a, nr);
}
#include "native-ccm-mbuf.inc"
#include "native-ccm.inc"
#include "native-notify.inc"
#include "native-ccmp.inc"
static int
copyin(const void *in, void *out, size_t n)
{
	memcpy(out, in, n);
	return 0;
}
static void
ieee80211_key_update_begin(struct ieee80211com *ic)
{
	(void)ic;
}
static void
ieee80211_key_update_end(struct ieee80211com *ic)
{
	(void)ic;
}
static struct ieee80211_node *
ieee80211_ref_node(struct ieee80211_node *ni)
{
	return ni;
}
static void
ieee80211_free_node(struct ieee80211_node *ni)
{
	(void)ni;
}
static struct ieee80211_node *
ieee80211_find_node(void *t, const uint8_t *a)
{
	(void)t;
	(void)a;
	return NULL;
}
static int
ieee80211_crypto_newkey(struct ieee80211com *ic, unsigned type, unsigned flags,
			struct ieee80211_key *k)
{
	if (type != IEEE80211_CIPHER_AES_CCM)
		return 0;
	k->wk_flags = (uint16_t)(flags | IEEE80211_KEY_SWCRYPT);
	k->wk_cipher = &ccmp;
	if (!k->wk_private)
		k->wk_private = ccmp_attach(ic, k);
	return k->wk_private != NULL;
}
static int
ieee80211_crypto_setkey(struct ieee80211com *ic, struct ieee80211_key *k,
			const uint8_t *a)
{
	(void)ic;
	(void)a;
	return ccmp_setkey(k);
}
#include "native-setkey.inc"
/* The generic RX consumer supplies key selection and header pullup. */
static const struct ieee80211_cipher ieee80211_cipher_none = {0};
static struct mbuf *
m_pullup(struct mbuf *m, int n)
{
	uint8_t data[4096];
	if (n > m->m_pkthdr.len)
		return NULL;
	assert(m->m_pkthdr.len <= (int)sizeof(data));
	m_copydata(m, 0, m->m_pkthdr.len, data);
	memcpy(m->storage, data, (size_t)m->m_pkthdr.len);
	m->m_data = m->storage;
	m->m_len = m->m_pkthdr.len;
	m->m_next = NULL;
	return m;
}
#define IEEE80211_WEP_HDRLEN (IEEE80211_WEP_IVLEN + IEEE80211_WEP_KIDLEN)
#define IEEE80211_WEP_MINLEN                                                   \
	(sizeof(struct ieee80211_frame) + IEEE80211_WEP_IVLEN +                \
	 IEEE80211_WEP_KIDLEN + IEEE80211_WEP_CRCLEN)
#include "native-crypto-decap.inc"

#endif
