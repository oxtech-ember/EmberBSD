/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual native descriptor/core consumers with modeled DMA and MMIO. */
#include "rtw88-test-kernel.h"
#include <dev/ic/rtw88var.h>
#include "native-wme-core.inc"
static unsigned checks, failures, writes, syncs, reads, io_calls, fail_io,
    fail_after;
static uint8_t mmio[0x1000];
static unsigned *hook_lock;
#define CHECK(c)                                                               \
	do {                                                                   \
		checks++;                                                      \
		if (!(c)) {                                                    \
			failures++;                                            \
			fprintf(stderr, "FAIL line %u: %s\n",                  \
				(unsigned)__LINE__, #c);                       \
		}                                                              \
	} while (0)
static int
write2(void *cookie, uint32_t reg, uint16_t value)
{
	(void)cookie;
	if (hook_lock)
		assert(*hook_lock == 1);
	assert(reg + 2 <= sizeof(mmio));
	writes++;
	mmio[reg] = (uint8_t)value;
	mmio[reg + 1] = (uint8_t)(value >> 8);
	return 0;
}
static void
dmasync(void *cookie, struct rtw88_dma *dma, int flags)
{
	(void)cookie;
	(void)dma;
	(void)flags;
	syncs++;
}
static int
read1(void *cookie, uint32_t reg, uint8_t *value)
{
	(void)cookie;
	if (hook_lock)
		assert(*hook_lock == 1);
	assert(reg < sizeof(mmio));
	reads++;
	io_calls++;
	if (io_calls == fail_io && !fail_after)
		return EIO;
	*value = mmio[reg];
	return io_calls == fail_io ? EIO : 0;
}
static int
write1(void *cookie, uint32_t reg, uint8_t value)
{
	(void)cookie;
	if (hook_lock)
		assert(*hook_lock == 1);
	assert(reg < sizeof(mmio));
	io_calls++;
	if (io_calls == fail_io && !fail_after)
		return EIO;
	writes++;
	mmio[reg] = value;
	return io_calls == fail_io ? EIO : 0;
}
static int
write4(void *cookie, uint32_t reg, uint32_t value)
{
	(void)cookie;
	if (hook_lock)
		assert(*hook_lock == 1);
	assert(reg + 4 <= sizeof(mmio));
	io_calls++;
	if (io_calls == fail_io && !fail_after)
		return EIO;
	writes++;
	for (unsigned i = 0; i < 4; i++)
		mmio[reg + i] = (uint8_t)(value >> (8 * i));
	return io_calls == fail_io ? EIO : 0;
}
static const struct rtw88_bus_ops ops = {.read_1 = read1,
					 .write_1 = write1,
					 .write_2 = write2,
					 .write_4 = write4,
					 .dma_sync = dmasync};
static void
prepare(struct rtw88_softc *sc)
{
	memset(sc, 0, sizeof(*sc));
	memset(mmio, 0, sizeof(mmio));
	writes = syncs = reads = io_calls = fail_io = fail_after = 0;
	sc->sc_ops = &ops;
	sc->sc_hci = &wme_hci;
	sc->sc_resources_ready = sc->sc_running = sc->sc_started = 1;
	sc->sc_channel = 1;
	sc->sc_phy.channel = (struct rtw88_channel_tuple){1, 1, 20};
	sc->sc_basic_rates = 0xf;
	for (unsigned q = 0; q < RTW88_NTXQ; q++) {
		struct rtw88_txq *txq = &sc->sc_txq[q];
		txq->nbuf = RTW88_TX_SLOTS;
		for (unsigned i = 0; i < RTW88_TX_SLOTS; i++) {
			txq->buf[i].vaddr = calloc(1, RTW88_TX_BUF_SIZE);
			assert(txq->buf[i].vaddr);
			txq->buf[i].cookie = &txq->buf[i];
			txq->buf[i].size = RTW88_TX_BUF_SIZE;
			txq->buf[i].addr = 0x100000 + q * 0x100000 + i * 0x1000;
		}
		struct rtw88_dma *ring = &sc->sc_ring[rtw88_txq_hw[q].ring];
		ring->size = RTW88_TX_SLOTS * RTW8822C_TX_BUFFER_DESC_SIZE;
		ring->cookie = ring;
		ring->vaddr = calloc(1, ring->size);
		assert(ring->vaddr);
	}
}
static void
release(struct rtw88_softc *sc)
{
	for (unsigned q = 0; q < RTW88_NTXQ; q++) {
		for (unsigned i = 0; i < RTW88_TX_SLOTS; i++)
			free(sc->sc_txq[q].buf[i].vaddr);
		free(sc->sc_ring[rtw88_txq_hw[q].ring].vaddr);
	}
}

#define WME_NUM_AC 4
struct ieee80211_channel {
	unsigned number;
};
struct ifnet {
	void *if_softc;
};
struct ieee80211com {
	struct ifnet *ic_ifp;
	struct ieee80211_channel *ic_curchan;
};
struct rtw88_net {
	struct rtw88_softc *core;
	unsigned *lock;
	struct {
		int dying;
	} policy;
	int work_pending;
};
#define ieee80211_chan2ieee(ic, chan) ((void)(ic), (chan)->number)
#define mutex_enter(lock)                                                      \
	do {                                                                   \
		assert(*(lock) == 0);                                          \
		*(lock) = 1;                                                   \
	} while (0)
#define mutex_exit(lock)                                                       \
	do {                                                                   \
		assert(*(lock) == 1);                                          \
		*(lock) = 0;                                                   \
	} while (0)
#include "native-wme-net.inc"
static void
test_native_sta_hook(void)
{
	struct rtw88_softc sc;
	unsigned lock = 0;
	struct rtw88_net net = {.core = &sc, .lock = &lock};
	struct ifnet ifp = {.if_softc = &net};
	struct ieee80211_channel chan = {1};
	struct ieee80211com ic = {.ic_ifp = &ifp, .ic_curchan = &chan};
	struct chanAccParams params = {0};
	for (unsigned ac = 0; ac < 4; ac++) {
		params.cap_wmeParams[ac].wmep_aifsn = 2 + ac;
		params.cap_wmeParams[ac].wmep_logcwmin = 3;
		params.cap_wmeParams[ac].wmep_logcwmax = 4;
		params.cap_wmeParams[ac].wmep_txopLimit = 65535;
	}
	CHECK(native_sta_hook != NULL);
	if (!native_sta_hook)
		return;
	hook_lock = &lock;
	prepare(&sc);
	CHECK(native_sta_hook(&ic, &params, 20) == 0);
	CHECK(lock == 0);
	CHECK((uint16_t)(mmio[0x508] | mmio[0x509] << 8) == 0x4332);
	release(&sc);
	for (unsigned after = 0; after < 2; after++)
		for (unsigned failure = 1; failure <= 7; failure++) {
			prepare(&sc);
			fail_io = failure;
			fail_after = after;
			CHECK(native_sta_hook(&ic, &params, 9) == EIO);
			CHECK(sc.sc_edca_error == EIO && lock == 0);
			unsigned before = io_calls;
			CHECK(native_sta_hook(&ic, &params, 9) == EIO);
			CHECK(io_calls == before);
			release(&sc);
		}
	prepare(&sc);
	net.policy.dying = 1;
	CHECK(native_sta_hook(&ic, &params, 20) == ENXIO && io_calls == 0);
	net.policy.dying = 0;
	net.work_pending = 1;
	CHECK(native_sta_hook(&ic, &params, 20) == EBUSY && io_calls == 0);
	net.work_pending = 0;
	chan.number = 36;
	CHECK(native_sta_hook(&ic, &params, 9) == EBUSY && io_calls == 0);
	release(&sc);
	hook_lock = NULL;
}
static void
test_qos_descriptor(void)
{
	struct rtw88_softc sc;
	uint8_t frame[32] = {0x88, 0x02};
	prepare(&sc);
	frame[24] = 3;
	CHECK(rtw88_tx_raw(&sc, RTW88_TXQ_BE, frame, 26) == 0);
	const uint8_t *desc = sc.sc_txq[RTW88_TXQ_BE].buf[0].vaddr;
	CHECK(desc[5] == 3); /* Actual QoS TID3 remains QSEL3 on the BE ring. */
	unsigned before = writes, ds = syncs;
	frame[24] = 6;
	CHECK(rtw88_tx_raw(&sc, RTW88_TXQ_BE, frame, 26) == EOPNOTSUPP);
	CHECK(writes == before &&
	      syncs == ds); /* VO must not use the BE producer. */
	frame[24] = 0;
	CHECK(rtw88_tx_raw(&sc, RTW88_TXQ_BE, frame, 25) == EMSGSIZE);
	/* Addr4 moves QoS Control to byte30; TID3 still selects BE/QSEL3. */
	frame[1] = 3;
	frame[30] = 3;
	CHECK(rtw88_tx_raw(&sc, RTW88_TXQ_BE, frame, 31) == EMSGSIZE);
	CHECK(rtw88_tx_raw(&sc, RTW88_TXQ_BE, frame, 32) == 0);
	desc = sc.sc_txq[RTW88_TXQ_BE].buf[1].vaddr;
	CHECK(desc[5] == 3);
	for (unsigned tid = 1; tid < 16; tid++) {
		if (tid == 3)
			continue;
		frame[30] = (uint8_t)tid;
		before = writes;
		ds = syncs;
		CHECK(rtw88_tx_raw(&sc, RTW88_TXQ_BE, frame, 32) == EOPNOTSUPP);
		CHECK(writes == before && syncs == ds);
	}
	frame[30] = 3;
	CHECK(rtw88_tx_raw(&sc, RTW88_TXQ_MGMT, frame, 32) == EOPNOTSUPP);
	frame[1] |= 0x80;
	before = writes;
	ds = syncs;
	CHECK(rtw88_tx_raw(&sc, RTW88_TXQ_BE, frame, 32) == EOPNOTSUPP);
	CHECK(writes == before && syncs == ds);
	/* The two legacy paths retain their original queue selection. */
	frame[0] = 0x08;
	frame[1] = 2;
	CHECK(rtw88_tx_raw(&sc, RTW88_TXQ_BE, frame, 24) == 0);
	desc = sc.sc_txq[RTW88_TXQ_BE].buf[2].vaddr;
	CHECK(desc[5] == 0);
	frame[0] = 0xb0;
	CHECK(rtw88_tx_raw(&sc, RTW88_TXQ_MGMT, frame, 24) == 0);
	desc = sc.sc_txq[RTW88_TXQ_MGMT].buf[0].vaddr;
	CHECK(desc[5] == 18);
	sc.sc_txq[RTW88_TXQ_BE].nbuf = 0;
	before = writes;
	ds = syncs;
	CHECK(rtw88_tx_raw(&sc, RTW88_TXQ_BE, frame, 24) == ENXIO);
	CHECK(writes == before && syncs == ds);
	release(&sc);
}
/* A caller must choose the matching producer; no AC fallback is allowed. */
static void
test_all_tid_producers(void)
{
	static const unsigned queues[8] = {1, 2, 2, 1, 3, 3, 4, 4};
	struct rtw88_softc sc;
	uint8_t frame[26] = {0x88, 0x02};

	prepare(&sc);
	frame[22] = 0x30;
	frame[23] = 0x12;
	for (unsigned tid = 0; tid < 8; tid++) {
		unsigned q = queues[tid];
		frame[24] = (uint8_t)tid;
		unsigned slot = q < RTW88_NTXQ ? sc.sc_txq[q].wp : 0;
		CHECK(rtw88_tx_raw(&sc, q, frame, sizeof(frame)) == 0);
		if (q >= RTW88_NTXQ)
			continue;
		const uint8_t *pkt = sc.sc_txq[q].buf[slot].vaddr;
		CHECK(pkt[5] == tid && (pkt[33] & 0x80) == 0);
		CHECK((pkt[37] >> 4 | (unsigned)pkt[38] << 4) == 0x123);
		for (unsigned wrong = 1; wrong < 5; wrong++) {
			if (wrong == q || wrong >= RTW88_NTXQ)
				continue;
			unsigned before = writes, ds = syncs;
			CHECK(rtw88_tx_raw(&sc, wrong, frame, sizeof(frame)) ==
			      EOPNOTSUPP);
			CHECK(writes == before && syncs == ds);
		}
		unsigned before = writes, ds = syncs;
		sc.sc_txq[q].nbuf = 0;
		CHECK(rtw88_tx_raw(&sc, q, frame, sizeof(frame)) == ENXIO);
		CHECK(writes == before && syncs == ds);
		sc.sc_txq[q].nbuf = RTW88_TX_SLOTS;
	}
	frame[0] = 0x08;
	for (unsigned q = 2; q < 5; q++) {
		unsigned before = writes, ds = syncs;
		CHECK(rtw88_tx_raw(&sc, q, frame, 24) == EOPNOTSUPP);
		CHECK(writes == before && syncs == ds);
	}
	release(&sc);
}

static void
test_maps_and_edca(void)
{
	static const uint8_t acs[8] = {0, 1, 1, 0, 2, 2, 3, 3};
	static const uint8_t rings[8] = {1, 0, 0, 1, 2, 2, 3, 3};
	static const uint32_t regs[8] = {0x508, 0x50c, 0x50c, 0x508,
					 0x504, 0x504, 0x500, 0x500};
	struct rtw88_wme_map map;
	for (unsigned tid = 0; tid < 8; tid++) {
		CHECK(rtw88_wme_map(tid, &map) == 0);
		CHECK(map.ac == acs[tid] && map.ring == rings[tid] &&
		      map.qsel == tid && map.edca == regs[tid]);
	}
	struct rtw88_edca_params params[4] = {
	    {3, 4, 10, 0}, {7, 4, 10, 0}, {2, 3, 4, 94}, {2, 2, 3, 47}};
	uint32_t words[4];
	CHECK(rtw88_edca_encode(params, 9, 10, words) == 0);
	CHECK(words[0] == 0xa425 && words[1] == 0xa449 &&
	      words[2] == 0x5e431c && words[3] == 0x2f321c);
	CHECK(rtw88_edca_encode(params, 9, 16, words) == 0);
	CHECK(words[0] == 0xa42b && words[1] == 0xa44f &&
	      words[2] == 0x5e4322 && words[3] == 0x2f3222);
	map.ac = 99;
	CHECK(rtw88_wme_map(16, &map) == EINVAL && map.ac == 99);
	CHECK(rtw88_wme_map(8, &map) == EOPNOTSUPP && map.ac == 99);
	CHECK(rtw88_wme_map(0, NULL) == EINVAL);
	params[3].txop = 0x10000;
	words[0] = words[1] = words[2] = words[3] = 0xdeadbeef;
	CHECK(rtw88_edca_encode(params, 9, 10, words) == EINVAL);
	CHECK(words[0] == 0xdeadbeef && words[1] == 0xdeadbeef &&
	      words[2] == 0xdeadbeef && words[3] == 0xdeadbeef);
	params[3].txop = 47;
	CHECK(rtw88_edca_encode(params, 8, 10, words) == EINVAL);
	CHECK(rtw88_edca_encode(params, 9, 15, words) == EINVAL);
	CHECK(rtw88_edca_encode(NULL, 9, 10, words) == EINVAL);
	CHECK(rtw88_edca_encode(params, 9, 10, NULL) == EINVAL);
}
static uint32_t
reg32(uint32_t reg)
{
	return (uint32_t)mmio[reg] | (uint32_t)mmio[reg + 1] << 8 |
	       (uint32_t)mmio[reg + 2] << 16 | (uint32_t)mmio[reg + 3] << 24;
}
static void
test_edca_apply(void)
{
	struct rtw88_softc sc;
	struct rtw88_edca_params params[4] = {
	    {3, 4, 10, 0}, {7, 4, 10, 0}, {2, 3, 4, 94}, {2, 2, 3, 47}};
	prepare(&sc);
	mmio[0x522] = 0xa0;
	CHECK(rtw88_edca_apply(&sc, params, 9) == 0);
	CHECK(reads == 1 && writes == 6 && io_calls == 7 &&
	      mmio[0x522] == 0xa0);
	CHECK(reg32(0x508) == 0xa425 && reg32(0x50c) == 0xa449);
	CHECK(reg32(0x504) == 0x5e431c && reg32(0x500) == 0x2f321c);
	/* Reject an invalid late AC before even reading/pause-changing the
	 * device. */
	unsigned before = io_calls;
	params[3].ecw_min = 5;
	CHECK(rtw88_edca_apply(&sc, params, 9) == EINVAL && io_calls == before);
	params[3].ecw_min = 2;
	params[3].aifsn = 15;
	CHECK(rtw88_edca_apply(&sc, params, 20) == ERANGE &&
	      io_calls == before);
	params[3].aifsn = 2;
	sc.sc_channel = 36;
	CHECK(rtw88_edca_apply(&sc, params, 20) == EINVAL &&
	      io_calls == before);
	CHECK(rtw88_edca_apply(&sc, params, 9) == 0);
	CHECK(reg32(0x508) == 0xa42b && reg32(0x504) == 0x5e4322);
	sc.sc_started = 0;
	before = io_calls;
	CHECK(rtw88_edca_apply(&sc, params, 9) == ENXIO && io_calls == before);
	sc.sc_started = 1;
	sc.sc_stop_requested = 1;
	CHECK(rtw88_edca_apply(&sc, params, 9) == ENXIO && io_calls == before);
	release(&sc);
	/* Inject an error at every read/write boundary. No work follows
	 * failure. */
	for (unsigned after = 0; after <= 1; after++)
		for (unsigned failure = 1; failure <= 7; failure++) {
			prepare(&sc);
			mmio[0x522] = 0xa0;
			fail_io = failure;
			fail_after = after;
			CHECK(rtw88_edca_apply(&sc, params, 9) == EIO);
			CHECK(io_calls == failure);
			CHECK(sc.sc_edca_error == EIO);
			unsigned before = io_calls, ds = syncs, ws = writes;
			uint8_t frame[24] = {0x08, 0x02};
			CHECK(rtw88_edca_apply(&sc, params, 9) == EIO);
			CHECK(rtw88_tx_available(&sc, RTW88_TXQ_BE) == 0);
			CHECK(rtw88_tx_raw(&sc, RTW88_TXQ_BE, frame,
					   sizeof(frame)) == EIO);
			for (unsigned q = 0; q < RTW88_NTXQ; q++) {
				CHECK(rtw88_tx_available(&sc, q) == 0);
				CHECK(rtw88_tx_raw(&sc, q, frame,
						   sizeof(frame)) == EIO);
			}
			CHECK(io_calls == before && syncs == ds &&
			      writes == ws);
			CHECK(mmio[0x522] ==
			      ((failure == 1 || (failure == 2 && !after) ||
				(failure == 7 && after))
				   ? 0xa0
				   : 0xff));
			release(&sc);
		}
}
int
main(void)
{
	test_native_sta_hook();
	test_qos_descriptor();
	test_all_tid_producers();
	test_maps_and_edca();
	test_edca_apply();
	printf("Native RTL8822CE WME prerequisite: %u checks, %u failures\n",
	       checks, failures);
	return failures != 0;
}
