/* SPDX-License-Identifier: BSD-2-Clause */
/* Original actual-source management RX contract, AI-assisted. */
#include "rtw88-mbuf-test.h"
#include <dev/ic/rtw88_net_mbuf.h>
static unsigned checks, failures, calls, fail_at, live, devgets;
static const int max_linkhdr = 16;
#define CHECK(e) do { checks++; if (!(e)) { failures++; \
	fprintf(stderr, "line %d: %s\n", __LINE__, #e); } } while (0)

struct mbuf *
m_gethdr(int how, int type)
{
	struct mbuf *m;
	CHECK(how == M_DONTWAIT && type == MT_DATA);
	if (++calls == fail_at)
		return NULL;
	m = calloc(1, sizeof(*m));
	if (m != NULL) {
		m->m_data = m->storage;
		memset(m->storage, 0xa5, sizeof(m->storage));
		live++;
	}
	return m;
}

struct mbuf *
m_get(int how, int type)
{
	return m_gethdr(how, type);
}

void
test_external(struct mbuf *m, size_t size, int how)
{
	CHECK(how == M_DONTWAIT && (m->m_flags & M_EXT) == 0);
	CHECK(size <= RTW88_NET_MPDU_LIMIT);
	if (++calls == fail_at)
		return;
	char *p = malloc(size);
	if (p == NULL)
		return;
	memset(p, 0xa5, size);
	m->m_data = p;
	m->m_flags |= M_EXT;
	m->external_size = size;
	live++;
}

struct mbuf *
m_free(struct mbuf *m)
{
	struct mbuf *next = m->m_next;
	if ((m->m_flags & M_EXT) != 0) {
		free(m->m_data);
		live--;
	}
	free(m);
	live--;
	return next;
}

void
m_freem(struct mbuf *m)
{
	while (m != NULL)
		m = m_free(m);
}

void
m_set_rcvif(struct mbuf *m, struct ifnet *ifp)
{
	m->m_pkthdr.rcvif = ifp;
}

/* Unchanged native m_devget, with allocator entry points replaced above. */
#define m_devget native_devget
#include "rtw88-mbuf-devget-ref.inc"
#undef m_devget

struct mbuf *
m_devget(char *buf, int len, int off, struct ifnet *ifp)
{
	devgets++;
	return native_devget(buf, len, off, ifp);
}

static void
check_frame(size_t len, uint8_t type)
{
	uint8_t frame[RTW88_NET_MPDU_LIMIT], original[RTW88_NET_MPDU_LIMIT];
	struct ifnet ifp = { 7 };
	struct mbuf *m;
	unsigned i, used;

	for (i = 0; i < sizeof(frame); i++)
		frame[i] = (uint8_t)(i ^ (i >> 8));
	frame[0] = type;
	memcpy(original, frame, sizeof(frame));
	calls = devgets = fail_at = 0;
	m = rtw88_net_rx_mbuf(frame, len, &ifp);
	CHECK(m != NULL);
	if (m == NULL)
		return;
	CHECK(m->m_pkthdr.len == (int)len && m->m_pkthdr.rcvif == &ifp);
	CHECK(memcmp(frame, original, sizeof(frame)) == 0);
	if ((type & 0x0c) == 0) {
		/* The parser must see every IE through its first-segment end. */
		CHECK(m->m_len == (int)len && m->m_next == NULL);
		CHECK(((m->m_flags & M_EXT) != 0) == (len > MHLEN));
		CHECK(m->external_size == (len <= MHLEN ? 0 :
		    len <= MCLBYTES ? MCLBYTES : len));
		CHECK(calls == (len <= MHLEN ? 1U : 2U));
		if (m->m_len == (int)len)
			CHECK(memcmp(m->m_data, original, len) == 0);
	} else {
		CHECK(devgets == 1);
		used = 0;
		for (struct mbuf *n = m; n != NULL; n = n->m_next) {
			CHECK(used + (unsigned)n->m_len <= len);
			CHECK(memcmp(n->m_data, original + used, (size_t)n->m_len) == 0);
			used += (unsigned)n->m_len;
		}
		CHECK(used == len);
	}
	used = calls;
	m_freem(m);
	CHECK(live == 0);
	/* Every allocation failure must release all storage. */
	for (i = 1; i <= used; i++) {
		calls = devgets = 0;
		fail_at = i;
		m = rtw88_net_rx_mbuf(frame, len, &ifp);
		CHECK(m == NULL);
		m_freem(m);
		CHECK(live == 0);
	}
	fail_at = 0;
}

int
main(void)
{
	static const size_t lengths[] = {24, MHLEN - 1, MHLEN, MHLEN + 1,
	    MINCLSIZE - 1, MINCLSIZE, MCLBYTES - 1, MCLBYTES,
	    MCLBYTES + 1, RTW88_NET_MPDU_LIMIT};
	uint8_t frame[RTW88_NET_MPDU_LIMIT + 1] = {0x80};
	struct ifnet ifp = { 9 };

	for (unsigned i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
		check_frame(lengths[i], 0x80);
		check_frame(lengths[i], 0x50);
		check_frame(lengths[i], 0xb0);
		check_frame(lengths[i], 0x08);
	}
	calls = 0;
	CHECK(rtw88_net_rx_mbuf(NULL, 24, &ifp) == NULL);
	CHECK(rtw88_net_rx_mbuf(frame, 0, &ifp) == NULL);
	CHECK(rtw88_net_rx_mbuf(frame, 23, &ifp) == NULL);
	CHECK(rtw88_net_rx_mbuf(frame, sizeof(frame), &ifp) == NULL);
	CHECK(rtw88_net_rx_mbuf(frame, SIZE_MAX, &ifp) == NULL);
	CHECK(calls == 0 && live == 0);
	printf("rtw88 management mbuf: %u assertions, %u failures\n", checks, failures);
	return failures != 0;
}
