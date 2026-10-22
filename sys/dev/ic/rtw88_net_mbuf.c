/* SPDX-License-Identifier: BSD-2-Clause */
/* Original native RX allocation helper, AI-assisted. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <net/if.h>
#include <dev/ic/rtw88_net_mbuf.h>

struct mbuf *
rtw88_net_rx_mbuf(const uint8_t *frame, size_t len, struct ifnet *ifp)
{
	struct mbuf *m;

	if (frame == NULL || len > RTW88_NET_MPDU_LIMIT)
		return NULL;
	/* The policy owner already checked the current authorized BA peer. Keep
	 * the allocator boundary narrow as well: no generic short control input. */
	if (len < 24 && !(len == 20 && frame[0] == 0x84 &&
	    (frame[1] & ~0x08) == 0 && frame[16] == 4 &&
	    (frame[17] & 0x8f) == 0 && (frame[18] & 15) == 0))
		return NULL;
	if ((frame[0] & 0x0c) != 0)
		return m_devget((char *)__UNCONST(frame), (int)len, 0, ifp);
	/* Native management parsers use the first segment as the frame end. */
	m = m_gethdr(M_DONTWAIT, MT_DATA);
	if (m == NULL)
		return NULL;
	if (len > MHLEN) {
		if (len <= MCLBYTES)
			MCLGET(m, M_DONTWAIT);
		else
			MEXTMALLOC(m, len, M_DONTWAIT);
		if ((m->m_flags & M_EXT) == 0) {
			m_freem(m);
			return NULL;
		}
	}
	memcpy(mtod(m, void *), frame, len);
	m_set_rcvif(m, ifp);
	m->m_len = m->m_pkthdr.len = (int)len;
	return m;
}
