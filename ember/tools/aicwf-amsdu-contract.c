/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD; AI-assisted actual-source receive regression. */
#include <sys/types.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef __packed
#define __packed __attribute__((__packed__))
#endif
#ifndef le16toh
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define le16toh(x) (x)
#else
#define le16toh(x) __builtin_bswap16(x)
#endif
#endif
#include <net80211/ieee80211.h>
#include <net80211/ieee80211_amsdu.h>

#define __BIT(n) (1U << (n))
#define __BITS(h, l) ((__BIT(h) - __BIT(l)) | __BIT(h))
#define __SHIFTOUT(v, m) (((v) & (m)) >> __builtin_ctz(m))
#define __UNCONST(p) ((void *)(uintptr_t)(const void *)(p))
#define le16dec(p) ((uint16_t)((p)[0] | (p)[1] << 8))
#define be16dec(p) ((uint16_t)((p)[1] | (p)[0] << 8))
#define le32dec(p) ((uint32_t)((p)[0] | (p)[1] << 8 | (p)[2] << 16 | (uint32_t)(p)[3] << 24))
#define roundup(n, m) (((n) + (m) - 1) / (m) * (m))
#define ETHER_ADDR_LEN 6
#define ETHER_HDR_LEN 14
#define ETHER_ALIGN 2
#define ETHER_IS_MULTICAST(p) ((p)[0] & 1)
#define MCLBYTES 2048
#define MHLEN 200
#define M_EXT 1
#define mtod(m, t) ((t)((m)->m_data))
#define MGETHDR(m, w, t) ((m) = newbuf())
#define MCLGET(m, w) do { if (++clusters != cluster_fail) (m)->m_flags |= M_EXT; } while (0)
#define if_statinc(p, name) ((p)->name++)
#define IEEE80211_DPRINTF(...) ((void)0)
static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); \
} } while (0)
struct ether_header { uint8_t ether_dhost[6], ether_shost[6]; uint16_t ether_type; } __packed;
struct ifnet { void *if_percpuq; unsigned if_ierrors; };
struct mbuf {
	uint8_t storage[MCLBYTES], *m_data;
	size_t m_len;
	int m_flags;
	struct { size_t len; struct ifnet *rcvif; } m_pkthdr;
	struct mbuf *m_nextpkt;
};
struct aicwf_softc {
	struct ifnet sc_if;
	struct { uint8_t ic_myaddr[6]; } sc_ic;
	struct { unsigned ev_count; } sc_ev_amsdu, sc_ev_ampdu;
	bool sc_if_attached;
	uint8_t sc_vif;
	unsigned sc_alive_rx;
	uint64_t sc_sae_epoch;
};
static unsigned allocations, frees, live, alloc_fail, clusters, cluster_fail;
static unsigned delivered, reordered, flushed, auths, mgmts;
static unsigned last_tid, last_seq;
static struct mbuf *held;
static uint8_t outputs[180][MCLBYTES];
static size_t output_lengths[180];
static struct ifnet *receive_if;
static const unsigned ticks = 123;

static struct mbuf *
newbuf(void)
{
	struct mbuf *m;
	if (++allocations == alloc_fail)
		return NULL;
	m = calloc(1, sizeof(*m));
	CHECK(m != NULL);
	m->m_data = m->storage;
	live++;
	return m;
}

static void
m_freem(struct mbuf *m)
{
	CHECK(m != NULL && live > 0);
	live--; frees++;
	free(m);
}

static void
m_set_rcvif(struct mbuf *m, struct ifnet *ifp)
{
	m->m_pkthdr.rcvif = ifp;
}

static int splnet(void) { return 0; }
static void splx(int s) { CHECK(s == 0); }

static void
if_percpuq_enqueue(const void *q, struct mbuf *m)
{
	CHECK(q == receive_if && m->m_pkthdr.rcvif == receive_if);
	CHECK(m->m_data == m->storage + ETHER_ALIGN && m->m_nextpkt == NULL);
	CHECK(m->m_len == m->m_pkthdr.len && m->m_len <= MCLBYTES);
	CHECK(delivered < 180);
	memcpy(outputs[delivered], m->m_data, m->m_len);
	output_lengths[delivered++] = m->m_len;
	m_freem(m);
}

static void
aicwf_reorder(const struct aicwf_softc *sc, u_int tid, uint16_t seq, struct mbuf *m)
{
	CHECK(sc->sc_sae_epoch == 77 && held == NULL);
	held = m; reordered++; last_tid = tid; last_seq = seq;
}

static void
aicwf_reorder_flush(const struct aicwf_softc *sc, int tid, bool deliver)
{
	CHECK(sc->sc_sae_epoch == 77 && tid == 5 && deliver);
	flushed++;
}

static void
aicwf_sae_event(const struct aicwf_softc *sc, bool confirm, const uint8_t *p, size_t n)
{
	CHECK(sc->sc_sae_epoch == 77 && !confirm && p[0] == 0xb0 && n >= 2);
	auths++;
}

static void
aicwf_mgmt_rx(const struct aicwf_softc *sc, const uint8_t *p, size_t n, uint32_t status, uint32_t flags)
{
	CHECK(sc->sc_sae_epoch == 77 && p[0] == 0xd0 && n >= 24);
	CHECK(status == 0 && flags != 0);
	mgmts++;
}

#include "aicwf-rx-under-test.c"

static struct aicwf_softc device;
static uint8_t packet[60 + 26 + 8 + 4096];
static const uint8_t station[6] = { 2, 1, 2, 3, 4, 5 };
static const uint8_t sender[6] = { 2, 9, 8, 7, 6, 5 };

static void
reset(void)
{
	CHECK(live == 0 && held == NULL);
	memset(&device, 0, sizeof(device));
	memcpy(device.sc_ic.ic_myaddr, station, 6);
	device.sc_if_attached = true;
	device.sc_sae_epoch = 77;
	receive_if = &device.sc_if;
	device.sc_if.if_percpuq = receive_if;
	allocations = frees = alloc_fail = clusters = cluster_fail = 0;
	delivered = reordered = flushed = auths = mgmts = 0;
	memset(packet, 0, sizeof(packet));
	packet[AICWF_RXHDR_FLAGS] = AICWF_RX_FLAG_UPLOAD;
	uint8_t *wh = packet + AICWF_RXHDR_LEN;
	wh[0] = 0x88; wh[1] = 2;
	memcpy(wh + 4, station, 6);
	memcpy(wh + 16, sender, 6);
	wh[22] = 0x30; wh[23] = 0x12; wh[24] = 0x85;
}

static size_t
subframe(uint8_t *p, size_t payload)
{
	CHECK(payload <= 1500);
	memcpy(p, station, 6);
	memcpy(p + 6, sender, 6);
	size_t n = payload + 8;
	p[12] = n >> 8; p[13] = n;
	memcpy(p + 14, "\xaa\xaa\x03\0\0\0\x08\0", 8);
	memset(p + 22, 0x5a, payload);
	return 22 + payload;
}

static void
receive(size_t body)
{
	aicwf_rx_data(&device, packet, 26 + body);
	CHECK(device.sc_sae_epoch == 77);
}

static void
rejected(void)
{
	CHECK(delivered == 0 && reordered == 0 && held == NULL);
	CHECK(live == 0 && device.sc_if.if_ierrors == 1);
}

int
main(void)
{
	uint8_t *body = packet + AICWF_RXHDR_LEN + 26;
	size_t n;

	/* Baseline must fail here: its valid prefix is delivered on a bad tail. */
	reset(); subframe(body, 2); subframe(body + 24, 0); body[24 + 13] = 0xff;
	receive(46); rejected(); CHECK(allocations == 0);

	reset(); subframe(body, 0); body[22] = 0x51; body[23] = 0xa7;
	subframe(body + 24, 0); receive(46);
	CHECK(delivered == 2 && flushed == 1 && device.sc_if.if_ierrors == 0);
	CHECK(output_lengths[0] == 14 && memcmp(outputs[0], station, 6) == 0);
	CHECK(memcmp(outputs[0] + 6, sender, 6) == 0 && outputs[0][12] == 8);
	CHECK(live == 0 && frees == 2);

	for (unsigned fail = 1; fail <= 2; fail++) {
		reset(); subframe(body, 2); subframe(body + 24, 0); alloc_fail = fail;
		receive(46); rejected(); CHECK(frees == fail - 1);
	}
	reset(); n = subframe(body, 200); cluster_fail = 1;
	receive(n); rejected(); CHECK(frees == 1);

	reset(); subframe(body, 0); body[19] = 0xf8; receive(22);
	CHECK(delivered == 1 && device.sc_if.if_ierrors == 0);
	for (unsigned kind = 0; kind < 6; kind++) {
		reset(); n = subframe(body, 0);
		switch (kind) {
		case 0: n++; break; /* Trailing padding is not a subframe. */
		case 1: body[0] = 4; break;
		case 2: body[6] = 3; break;
		case 3: body[19] = 1; break;
		case 4: body[20] = 0x88; body[21] = 0x8e; break;
		case 5: memcpy(body, "\xaa\xaa\x03\0\0\0", 6); break;
		}
		receive(n); rejected(); CHECK(allocations == 0);
	}

	/* The declared AIC HT/VHT profile admits this maximum aggregate. */
	for (unsigned extra = 0; extra < 2; extra++) {
		reset(); subframe(body, 1500); subframe(body + 1524, 1500);
		subframe(body + 3048, 769 + extra); receive(3839 + extra);
		if (extra == 0) CHECK(delivered == 3 && live == 0);
		else { rejected(); CHECK(allocations == 0); }
	}

	/* Descriptor flags, sequence, TID and firmware decrypt offset persist. */
	reset(); packet[AICWF_RXHDR_FLAGS] |= AICWF_RX_FLAG_REORDER;
	subframe(body, 0); receive(22);
	CHECK(reordered == 1 && delivered == 0 && last_tid == 5 && last_seq == 0x123);
	CHECK(device.sc_ev_ampdu.ev_count == 1 && live == 1);
	aicwf_rx_free(held); held = NULL;
	for (unsigned decrypt = 0; decrypt < 2; decrypt++) {
		reset(); packet[AICWF_RXHDR_STATUS] = (decrypt ? 3 : AICWF_RX_DECR_WEP) << 2;
		size_t iv = decrypt ? 8 : 4;
		subframe(body + iv, 0); receive(22 + iv);
		CHECK(delivered == 1 && live == 0);
	}
	reset(); packet[AICWF_RXHDR_FLAGS] = 0; subframe(body, 0); receive(22);
	CHECK(delivered == 0 && allocations == 0);
	reset(); device.sc_if_attached = false; subframe(body, 0); receive(22);
	CHECK(delivered == 0 && allocations == 0);

	/* Individual EAPOL and management keep their existing consumers. */
	reset(); packet[AICWF_RXHDR_LEN + 24] = 5;
	memcpy(body, "\xaa\xaa\x03\0\0\0\x88\x8e", 8); receive(8);
	CHECK(delivered == 1 && outputs[0][12] == 0x88 && outputs[0][13] == 0x8e);
	reset(); packet[AICWF_RXHDR_LEN] = 0xb0; receive(0);
	CHECK(auths == 1 && allocations == 0);
	reset(); packet[AICWF_RXHDR_LEN] = 0xd0; receive(0);
	CHECK(mgmts == 1 && allocations == 0);
	CHECK(live == 0);
	printf("aicwf A-MSDU: %u checks passed\n", checks);
	return 0;
}
