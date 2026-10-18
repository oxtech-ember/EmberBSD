/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD; AI-assisted production CEMAC IRQ lifecycle tests. */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define __BIT(n) (UINT32_C(1) << (n))
#define __BITS(hi, lo) ((UINT32_MAX >> (31 - (hi))) & (UINT32_MAX << (lo)))
#define __SHIFTIN(v, mask) ((v) * ((mask) & -(mask)))
#define ISSET(v, bits) ((v) & (bits))
#define ETHER_ADDR_LEN 6
#define MAXCOMLEN 16
#define IFF_UP 1
#define IFF_RUNNING 2
#define IFM_ACTIVE 1
#define IFCAP_CSUM_IPv4_Tx 1
#define IFCAP_CSUM_TCPv4_Tx 2
#define IFCAP_CSUM_UDPv4_Tx 4
#define IFCAP_CSUM_TCPv6_Tx 8
#define IFCAP_CSUM_UDPv6_Tx 16
#define IFCAP_CSUM_IPv4_Rx 32
#define IFCAP_CSUM_TCPv4_Rx 64
#define IFCAP_CSUM_UDPv4_Rx 128
#define IFCAP_CSUM_TCPv6_Rx 256
#define IFCAP_CSUM_UDPv6_Rx 512
#define MCLBYTES 2048
#define M_EXT 1
#define M_DONTWAIT 0
#define MT_DATA 0
#define M_CSUM_IPv4 1
#define M_CSUM_UDPv4 2
#define M_CSUM_UDPv6 4
#define M_CSUM_TCPv4 8
#define M_CSUM_TCPv6 16
#define BUS_DMASYNC_POSTREAD 1
#define BUS_DMASYNC_POSTWRITE 2
#define BUS_DMASYNC_PREREAD 4
#define BUS_DMASYNC_PREWRITE 8
#define BUS_DMA_NOWAIT 0
#define DPRINTFN(n, args) ((void)0)
typedef unsigned short u_short;
typedef void *device_t;
typedef void *bus_space_tag_t;
typedef uintptr_t bus_space_handle_t;
typedef void *bus_dma_tag_t;
typedef uintptr_t bus_addr_t;
typedef struct { bool held; } kmutex_t;
typedef struct { bool pending; } callout_t;
struct work { unsigned value; };
struct dma_map {
	struct { uintptr_t ds_addr; } dm_segs[1];
	bool mapped, device_owned;
	unsigned loads, unloads, postreads, prereads;
};
typedef struct dma_map *bus_dmamap_t;
struct mbuf {
	unsigned m_flags;
	int m_len;
	struct { int len; unsigned csum_flags; } m_pkthdr;
	struct { void *ext_buf; } m_ext;
};
struct ifnet {
	void *if_softc, *if_percpuq;
	unsigned if_flags, if_capenable;
	unsigned if_ierrors, if_ipackets, if_collisions;
	char if_xname[16];
	void (*if_start)(struct ifnet *);
	bool locked;
};
struct ethercom { struct ifnet ec_if; };
typedef struct mii_data { unsigned mii_media_status; } mii_data_t;
#include "cemacreg.h"
#include "if_cemacvar.h"

static unsigned checks;
#define CHECK(e) do { checks++; if (!(e)) { \
	fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #e); abort(); \
} } while (0)
#define KASSERT(e) CHECK(e)
#define ASSERT_SLEEPABLE() CHECK(!intr_lock.held && !mcast_lock.held)
#define IFNET_LOCKED(ifp) ((ifp)->locked)
#define IFNET_LOCK(ifp) do { CHECK(!(ifp)->locked); (ifp)->locked = true; } while (0)
#define IFNET_UNLOCK(ifp) do { CHECK((ifp)->locked); (ifp)->locked = false; } while (0)
static kmutex_t intr_lock, mcast_lock;
static struct cemac_softc test_sc;
static uint32_t registers[ETH_SIZE / 4], posted_mask;
static bool read_clear, pending_disable;
static unsigned writes, isr_reads, halts, media_calls, ticks, starts, queued;
static unsigned dma_calls, frees, deferred, delivered;
static bool rx_enabled, in_handler;
static unsigned alloc_failure, alloc_attempts;
static struct mbuf *retained_mbufs[RX_QLEN];
static int media_error;
static time_t time_uptime = 20;
static const int hz = 100, cemac_watchdog_timeout = 5;
static const uint32_t irq_mask = ETH_ISR_RCOM | ETH_ISR_TBRE | ETH_ISR_TIDLE |
	ETH_ISR_RBNA | ETH_ISR_ROVR | ETH_ISR_TCOM;

static bool
mutex_owned(kmutex_t *lock) { return lock->held; }
static void
mutex_enter(kmutex_t *lock) { CHECK(!lock->held); lock->held = true; }
static void
mutex_exit(kmutex_t *lock) { CHECK(lock->held); lock->held = false; }

/* Model PCIe posted IDR writes. Only a subsequent register read flushes them. */
static uint32_t
reg_read(unsigned reg)
{
	uint32_t value;
	CHECK(reg < ETH_SIZE && reg % 4 == 0);
	if (pending_disable) {
		registers[ETH_IMR / 4] |= posted_mask;
		pending_disable = false;
	}
	value = registers[reg / 4];
	if (reg == ETH_ISR) {
		isr_reads++;
		if (read_clear)
			registers[reg / 4] = 0;
	}
	return value;
}
static void
reg_write(unsigned reg, uint32_t value)
{
	CHECK(reg < ETH_SIZE && reg % 4 == 0);
	writes++;
	switch (reg) {
	case ETH_IDR:
		posted_mask = value;
		pending_disable = true;
		break;
	case ETH_IER:
		/* No IRQ may run before the software service gate is open. */
		CHECK(intr_lock.held && !test_sc.sc_stopping && media_calls > 0);
		CHECK(media_error == 0 && !pending_disable);
		registers[ETH_IMR / 4] &= ~value;
		break;
	case ETH_ISR:
		if (!read_clear)
			registers[reg / 4] &= ~value;
		break;
	case ETH_TSR:
	case ETH_RSR:
		registers[reg / 4] &= ~value;
		break;
	default:
		registers[reg / 4] = value;
		break;
	}
}
#define CEMAC_READ(reg) reg_read(reg)
#define CEMAC_WRITE(reg, value) reg_write(reg, value)

static void cemac_tick(void *);
static int cemac_ifinit(struct ifnet *);
static void
callout_halt(callout_t *callout, void *interlock)
{
	CHECK(!intr_lock.held && !mcast_lock.held && interlock == NULL);
	CHECK(test_sc.sc_stopping);
	CHECK(!pending_disable && (registers[ETH_IMR / 4] & irq_mask) == irq_mask);
	callout->pending = false;
	halts++;
	/* An already dispatched tick must return without rearming itself. */
	cemac_tick(&test_sc);
	CHECK(!callout->pending);
}
static void
callout_reset(callout_t *callout, int delay, void (*fn)(void *), void *arg)
{
	CHECK(delay == hz && fn == cemac_tick && arg == &test_sc);
	CHECK(!test_sc.sc_stopping && media_error == 0);
	callout->pending = true;
}
static void
callout_schedule(callout_t *callout, int delay)
{ CHECK(delay == hz); callout->pending = true; }
static void
mii_down(mii_data_t *mii)
{ CHECK(!intr_lock.held && halts > 0); }
static int
mii_mediachg(mii_data_t *mii)
{
	CHECK(!intr_lock.held && test_sc.sc_stopping);
	CHECK((registers[ETH_IMR / 4] & irq_mask) == irq_mask);
	media_calls++;
	return media_error;
}
static void mii_tick(mii_data_t *mii) { ticks++; }
static unsigned
atomic_swap_uint(volatile unsigned *p, unsigned value)
{ unsigned old = *p; *p = value; return old; }
static void atomic_store_relaxed(volatile unsigned *p, unsigned value) { *p = value; }
static void workqueue_enqueue(struct workqueue *q, struct work *work, void *cpu)
{ CHECK(work == &test_sc.sc_reset_work && cpu == NULL); queued++; }
static void start_callback(struct ifnet *ifp) { CHECK(!test_sc.sc_stopping); starts++; }

/* A bounded RX model checks the real interrupt loop and buffer ownership. */
static struct mbuf *
get_header(void)
{
	struct mbuf *m;

	CHECK(rx_enabled);
	if (in_handler) {
		CHECK(++alloc_attempts <= RX_QLEN);
		if (alloc_failure == 1)
			return NULL;
	}
	m = calloc(1, sizeof(*m));
	CHECK(m != NULL);
	return m;
}
static void
get_cluster(struct mbuf *m)
{
	CHECK(rx_enabled && m != NULL && m->m_ext.ext_buf == NULL);
	if (in_handler && alloc_failure == 2)
		return;
	m->m_ext.ext_buf = malloc(MCLBYTES);
	CHECK(m->m_ext.ext_buf != NULL);
	m->m_flags |= M_EXT;
}
#define MGETHDR(m, wait, type) ((m) = get_header())
#define MCLGET(m, wait) get_cluster(m)
#define mtod(m, type) ((type)(m)->m_ext.ext_buf)
static void m_adj(struct mbuf *m, int length) { abort(); }
static void
m_set_rcvif(struct mbuf *m, struct ifnet *ifp)
{
	CHECK(rx_enabled && m != NULL && ifp == &test_sc.sc_ethercom.ec_if);
}
static void
m_freem(struct mbuf *m)
{
	CHECK(rx_enabled);
	if (m == NULL)
		return;
	if (in_handler && alloc_failure != 0) {
		for (unsigned i = 0; i < RX_QLEN; i++)
			CHECK(m != retained_mbufs[i]);
	}
	frees++;
	free(m->m_ext.ext_buf);
	free(m);
}
static void
if_percpuq_enqueue(void *q, struct mbuf *m)
{
	CHECK(rx_enabled && m->m_len == 60 && m->m_pkthdr.len == 60);
	CHECK(m->m_pkthdr.csum_flags == 0);
	delivered++;
	m_freem(m);
}
static void
bus_dmamap_sync(bus_dma_tag_t t, bus_dmamap_t m,
	unsigned o, unsigned length, unsigned flags)
{
	CHECK(rx_enabled && intr_lock.held && m != NULL && m->mapped && o == 0);
	if (m == test_sc.rbqpage_dmamap) {
		CHECK(length == test_sc.rbqlen);
		CHECK(flags == (BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE) ||
			flags == (BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE));
	} else {
		CHECK(length == MCLBYTES);
		CHECK(flags == BUS_DMASYNC_POSTREAD || flags == BUS_DMASYNC_PREREAD);
	}
	if (flags & BUS_DMASYNC_POSTREAD) {
		CHECK(m->device_owned);
		m->device_owned = false;
		m->postreads++;
	} else {
		CHECK(!m->device_owned);
		m->device_owned = true;
		m->prereads++;
	}
	dma_calls++;
}
static void
bus_dmamap_unload(bus_dma_tag_t t, bus_dmamap_t m)
{
	CHECK(rx_enabled && intr_lock.held && m != NULL && m->mapped);
	CHECK(!m->device_owned);
	m->mapped = false;
	m->unloads++;
}
static int
bus_dmamap_load(bus_dma_tag_t t, bus_dmamap_t m,
	void *buffer, unsigned length, void *p, unsigned flags)
{
	CHECK(rx_enabled && intr_lock.held && m != NULL && !m->mapped);
	CHECK(buffer != NULL && length == MCLBYTES && p == NULL);
	CHECK(flags == BUS_DMA_NOWAIT);
	m->mapped = true;
	m->loads++;
	return 0;
}
static void if_schedule_deferred_start(struct ifnet *ifp) { deferred++; }
typedef unsigned net_stat_ref_t;
#define IF_STAT_GETREF(ifp) 0U
#define IF_STAT_PUTREF(ifp) ((void)0)
#define if_statinc_ref(ifp, ref, field) ((void)(ref), (ifp)->field++)
#define if_statadd(ifp, field, value) ((ifp)->field += (value))
#define aprint_normal_ifnet(...) ((void)0)
#include "cemac-bodies.h"

static void
reset(bool clear)
{
	memset(&test_sc, 0, sizeof(test_sc));
	memset(registers, 0, sizeof(registers));
	intr_lock.held = mcast_lock.held = false;
	test_sc.sc_intr_lock = &intr_lock;
	test_sc.sc_mcast_lock = &mcast_lock;
	test_sc.cemac_flags = CEMAC_FLAG_GEM;
	test_sc.sc_ethercom.ec_if.if_softc = &test_sc;
	test_sc.sc_ethercom.ec_if.if_flags = IFF_UP | IFF_RUNNING;
	test_sc.sc_ethercom.ec_if.if_start = start_callback;
	memcpy(test_sc.sc_ethercom.ec_if.if_xname, "cemac-test", 11);
	test_sc.sc_ethercom.ec_if.locked = true;
	test_sc.cemac_tick_ch.pending = true;
	registers[ETH_IMR / 4] = ~irq_mask;
	registers[ETH_CTL / 4] = ETH_CTL_TE | ETH_CTL_RE | ETH_CTL_MPE;
	registers[ETH_CFG / 4] = GEM_CFG_CLK_96 | GEM_CFG_DBW_64 | GEM_CFG_GEN;
	registers[ETH_RBQP / 4] = 0x20000;
	registers[ETH_TBQP / 4] = 0x30000;
	registers[ETH_ISR / 4] = ETH_ISR_TCOM;
	pending_disable = false;
	posted_mask = 0;
	read_clear = clear;
	writes = isr_reads = halts = media_calls = ticks = starts = queued = 0;
	dma_calls = frees = deferred = delivered = 0;
	rx_enabled = in_handler = false;
	alloc_failure = alloc_attempts = 0;
	memset(retained_mbufs, 0, sizeof(retained_mbufs));
	media_error = 0;
}
static void
check_retained(void)
{
	CHECK(registers[ETH_RBQP / 4] == 0x20000);
	CHECK(registers[ETH_TBQP / 4] == 0x30000);
	CHECK(registers[ETH_CFG / 4] == (GEM_CFG_CLK_96 | GEM_CFG_DBW_64 | GEM_CFG_GEN));
	CHECK(dma_calls == 0 && frees == 0);
}
static void
test_stop(bool clear)
{
	reset(clear);
	test_sc.sc_ethercom.ec_if.if_flags &= ~IFF_UP;
	cemac_ifstop(&test_sc.sc_ethercom.ec_if, 1);
	CHECK(test_sc.sc_stopping && !test_sc.cemac_tick_ch.pending);
	CHECK((test_sc.sc_ethercom.ec_if.if_flags & IFF_RUNNING) == 0);
	CHECK(registers[ETH_ISR / 4] == ETH_ISR_TCOM && isr_reads == 0);
	CHECK(!pending_disable && registers[ETH_IMR / 4] == UINT32_MAX);
	check_retained();
	/* A new device cause stays masked; a shared xHCI IRQ can still arrive. */
	registers[ETH_ISR / 4] = ETH_ISR_RCOM;
	unsigned saved = writes;
	CHECK(cemac_intr(&test_sc) == 0);
	CHECK(writes == saved && !intr_lock.held);
	CHECK((registers[ETH_ISR / 4] & ~registers[ETH_IMR / 4]) == 0);
	cemac_ifstop(&test_sc.sc_ethercom.ec_if, 1);
	CHECK(registers[ETH_ISR / 4] == ETH_ISR_RCOM && halts == 2);
}
static void
test_init(bool clear, int error)
{
	reset(clear);
	media_error = error;
	CHECK(cemac_ifinit(&test_sc.sc_ethercom.ec_if) == error);
	CHECK(media_calls == 1);
	check_retained();
	if (error != 0) {
		CHECK(test_sc.sc_stopping && !test_sc.cemac_tick_ch.pending);
		CHECK(registers[ETH_IMR / 4] == UINT32_MAX);
		CHECK((test_sc.sc_ethercom.ec_if.if_flags & IFF_RUNNING) == 0);
		return;
	}
	CHECK(!test_sc.sc_stopping && test_sc.cemac_tick_ch.pending);
	CHECK(registers[ETH_IMR / 4] == ~irq_mask);
	CHECK(registers[ETH_ISR / 4] == ETH_ISR_TCOM && isr_reads == 0);
	CHECK(cemac_intr(&test_sc) == 1);
	CHECK(registers[ETH_ISR / 4] == 0 && deferred == 1);
	CHECK(cemac_intr(&test_sc) == 0); /* The shared IRQ is not ours. */
	CHECK(!intr_lock.held);
}
static void
test_media(bool clear, bool up, int error)
{
	reset(clear);
	media_error = error;
	if (!up)
		test_sc.sc_ethercom.ec_if.if_flags &= ~IFF_UP;
	CHECK(cemac_mediachange(&test_sc.sc_ethercom.ec_if) == (up ? error : 0));
	CHECK(media_calls == (up ? 1U : 0U));
	if (!up)
		CHECK(writes == 0);
	else
		CHECK(test_sc.sc_stopping == (error != 0));
}
static void
test_reset(bool clear, bool up, int error)
{
	reset(clear);
	test_sc.sc_tx_sending = true;
	test_sc.sc_tx_lastsent = 0;
	cemac_tick(&test_sc);
	CHECK(test_sc.sc_reset_pending == 1 && queued == 1);
	if (!up)
		test_sc.sc_ethercom.ec_if.if_flags &= ~IFF_UP;
	cemac_ifstop(&test_sc.sc_ethercom.ec_if, 1);
	test_sc.sc_ethercom.ec_if.locked = false;
	media_error = error;
	unsigned saved = writes;
	cemac_handle_reset_work(&test_sc.sc_reset_work, &test_sc);
	CHECK(test_sc.sc_reset_pending == 0 && !test_sc.sc_ethercom.ec_if.locked);
	if (!up) {
		CHECK(writes == saved && media_calls == 0 && starts == 0);
		CHECK(test_sc.sc_stopping && registers[ETH_IMR / 4] == UINT32_MAX);
	} else {
		CHECK(media_calls == 1 && starts == (error == 0 ? 1U : 0U));
		CHECK(test_sc.sc_stopping == (error != 0));
	}
	check_retained();
}
static void
test_rx_pending(bool clear, unsigned failure)
{
	struct eth_dsc ring[RX_QLEN];
	struct dma_map ringmap = { .mapped = true, .device_owned = true };
	struct dma_map maps[RX_QLEN];

	reset(clear);
	test_sc.sc_ethercom.ec_if.if_flags &= ~IFF_UP;
	cemac_ifstop(&test_sc.sc_ethercom.ec_if, 1);
	check_retained();
	/* RX DMA can fill every retained descriptor while IRQs are masked. */
	rx_enabled = true;
	test_sc.RDSC = ring;
	test_sc.rbqlen = sizeof(ring);
	test_sc.rbqpage_dmamap = &ringmap;
	memset(maps, 0, sizeof(maps));
	for (unsigned i = 0; i < RX_QLEN; i++) {
		test_sc.rxq[i].m = get_header();
		get_cluster(test_sc.rxq[i].m);
		retained_mbufs[i] = test_sc.rxq[i].m;
		maps[i].mapped = maps[i].device_owned = true;
		maps[i].dm_segs[0].ds_addr = 0x40000 + i * MCLBYTES;
		test_sc.rxq[i].m_dmamap = &maps[i];
		ring[i].Addr = maps[i].dm_segs[0].ds_addr | ETH_RDSC_F_USED |
			(i == RX_QLEN - 1 ? ETH_RDSC_F_WRAP : 0);
		ring[i].Info = 64;
	}
	registers[ETH_ISR / 4] = ETH_ISR_RCOM | ETH_ISR_RBNA;
	CHECK(cemac_intr(&test_sc) == 0);
	CHECK(delivered == 0 && isr_reads == 0);
	test_sc.sc_ethercom.ec_if.if_flags |= IFF_UP;
	CHECK(cemac_ifinit(&test_sc.sc_ethercom.ec_if) == 0);
	/* No new arrival is needed: the retained cause must assert IRQ now. */
	CHECK((registers[ETH_ISR / 4] & ~registers[ETH_IMR / 4]) != 0);
	alloc_failure = failure;
	in_handler = true;
	CHECK(cemac_intr(&test_sc) == 1);
	in_handler = false;
	CHECK(alloc_attempts == RX_QLEN && test_sc.rxqi == RX_QLEN);
	CHECK(delivered == (failure == 0 ? RX_QLEN : 0));
	CHECK(test_sc.sc_stat_rx_nombuf == (failure != 0 ? RX_QLEN : 0));
	CHECK(test_sc.sc_ethercom.ec_if.if_ierrors == 1 +
	    (failure != 0 ? RX_QLEN : 0));
	CHECK(test_sc.sc_stat_rx_bufna == 1 && registers[ETH_ISR / 4] == 0);
	CHECK(dma_calls == RX_QLEN * 2 + 2);
	CHECK(frees == (failure == 1 ? 0 : RX_QLEN));
	CHECK(ringmap.device_owned && ringmap.postreads == 1 && ringmap.prereads == 1);
	for (unsigned i = 0; i < RX_QLEN; i++) {
		CHECK(!(ring[i].Addr & ETH_RDSC_F_USED) && maps[i].mapped);
		CHECK(ring[i].Info == 0 && maps[i].device_owned);
		CHECK(maps[i].postreads == 1 && maps[i].prereads == 1);
		CHECK(maps[i].loads == (failure == 0 ? 1U : 0U));
		CHECK(maps[i].unloads == (failure == 0 ? 1U : 0U));
		if (failure != 0)
			CHECK(test_sc.rxq[i].m == retained_mbufs[i]);
		CHECK(ring[i].Addr == (maps[i].dm_segs[0].ds_addr |
			(i == RX_QLEN - 1 ? ETH_RDSC_F_WRAP : 0)));
	}
	if (failure != 0) {
		/* Allocation recovery must receive again without reinitializing. */
		alloc_failure = alloc_attempts = 0;
		ring[0].Addr |= ETH_RDSC_F_USED;
		ring[0].Info = 64;
		registers[ETH_ISR / 4] = ETH_ISR_RCOM;
		in_handler = true;
		CHECK(cemac_intr(&test_sc) == 1);
		in_handler = false;
		CHECK(delivered == 1 && alloc_attempts == 1);
		CHECK(test_sc.sc_stat_rx_nombuf == RX_QLEN);
		CHECK(test_sc.sc_ethercom.ec_if.if_ierrors == 1 + RX_QLEN);
		CHECK(!(ring[0].Addr & ETH_RDSC_F_USED) && maps[0].device_owned);
		CHECK(maps[0].postreads == 2 && maps[0].prereads == 2);
		CHECK(maps[0].loads == 1 && maps[0].unloads == 1);
	}
	for (unsigned i = 0; i < RX_QLEN; i++) {
		m_freem(test_sc.rxq[i].m);
		test_sc.rxq[i].m = NULL;
	}
	CHECK(frees == (failure == 1 ? RX_QLEN : 2U * RX_QLEN) +
	    (failure != 0 ? 1U : 0U));
}
int
main(int argc, char **argv)
{
	const char *which = argc > 1 ? argv[1] : "all";
	CHECK(strcmp(which, "all") == 0 || strcmp(which, "stop") == 0 ||
		strcmp(which, "init") == 0 || strcmp(which, "reset") == 0 ||
		strcmp(which, "rx") == 0 || strcmp(which, "media") == 0 ||
		strcmp(which, "nomem-header") == 0 || strcmp(which, "nomem-cluster") == 0);
	for (unsigned clear = 0; clear < 2; clear++) {
		if (strcmp(which, "all") == 0 || strcmp(which, "media") == 0) {
			test_media(clear, false, EIO);
			test_media(clear, true, 0);
			test_media(clear, true, EIO);
			test_media(clear, true, ENXIO);
		}
		if (strcmp(which, "all") == 0 || strcmp(which, "rx") == 0)
			test_rx_pending(clear, 0);
		if (strcmp(which, "all") == 0 || strcmp(which, "nomem-header") == 0)
			test_rx_pending(clear, 1);
		if (strcmp(which, "all") == 0 || strcmp(which, "nomem-cluster") == 0)
			test_rx_pending(clear, 2);
		if (strcmp(which, "all") == 0 || strcmp(which, "stop") == 0)
			test_stop(clear);
		if (strcmp(which, "all") == 0 || strcmp(which, "init") == 0) {
			test_init(clear, 0);
			test_init(clear, EIO);
			test_init(clear, ENXIO);
		}
		if (strcmp(which, "all") == 0 || strcmp(which, "reset") == 0) {
			test_reset(clear, false, 0);
			test_reset(clear, true, 0);
			test_reset(clear, true, EIO);
		}
	}
	printf("cemac IRQ lifecycle: PASS %u checks (%s)\n", checks, which);
	return 0;
}
