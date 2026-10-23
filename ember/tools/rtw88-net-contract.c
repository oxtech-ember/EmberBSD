/* SPDX-License-Identifier: BSD-2-Clause */
/* Original native RTL8822CE net ingress contracts, AI-assisted. */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include "rtw88_net_policy.h"


static unsigned checks, failures;
#define CHECK(e) do { checks++; if (!(e)) { failures++; \
	fprintf(stderr, "line %u: %s\n", (unsigned)__LINE__, #e); } } while (0)

struct ht20_snapshot_race {
	volatile uint64_t word;
	volatile unsigned done;
	unsigned writer_failures, reader_failures, reads;
};

static void *
ht20_snapshot_writer(void *arg)
{
	struct ht20_snapshot_race *race = arg;
	struct rtw88_net_ht20_snapshot p;
	uint64_t word;
	unsigned i;

	for (i = 1; i <= 200000; i++) {
		p.generation = i;
		p.rx_mcs = p.tx_mcs = i & 0xff;
		p.ready = i & RTW88_NET_HT20_READY_ALL;
		p.ampdu = (i & 7) << 2;
		if (rtw88_net_ht20_snapshot_encode(&p, &word) != 0) {
			race->writer_failures++;
			break;
		}
		__atomic_store_n(&race->word, word, __ATOMIC_RELEASE);
	}
	__atomic_store_n(&race->done, 1, __ATOMIC_RELEASE);
	return NULL;
}

static void *
ht20_snapshot_reader(void *arg)
{
	struct ht20_snapshot_race *race = arg;
	struct rtw88_net_ht20_snapshot p;
	uint64_t word;
	int error;

	do {
		word = __atomic_load_n(&race->word, __ATOMIC_ACQUIRE);
		if (word != 0) {
			error = rtw88_net_ht20_snapshot_decode(word, &p);
			if (error != 0 || p.generation == 0 ||
			    p.rx_mcs != (p.generation & 0xff) ||
			    p.tx_mcs != p.rx_mcs ||
			    p.ready != (p.generation & RTW88_NET_HT20_READY_ALL) ||
			    p.ampdu != ((p.generation & 7) << 2))
				race->reader_failures++;
			race->reads++;
		}
	} while (__atomic_load_n(&race->done, __ATOMIC_ACQUIRE) == 0);
	return NULL;
}

static void
ht20_snapshot_checks(void)
{
	static const uint8_t ready_bits[] = {
		RTW88_NET_HT20_READY_RADIO,
		RTW88_NET_HT20_READY_POWER,
		RTW88_NET_HT20_READY_PROTECTION,
		RTW88_NET_HT20_READY_RXBA,
		RTW88_NET_HT20_READY_SCHEDULER,
		RTW88_NET_HT20_READY_AMSDU
	};
	struct rtw88_net_ht20_snapshot p = {
	    .generation = 0x12345678, .rx_mcs = 0xff, .tx_mcs = 0xff,
	    .ready = RTW88_NET_HT20_READY_ALL, .ampdu = 0x10
	}, decoded;
	struct ht20_snapshot_race race = {0};
	pthread_t writer, reader;
	uint64_t word;
	size_t i;
	uint8_t ready_mask = 0;
	int writer_error, reader_error;

	CHECK(rtw88_net_ht20_snapshot_encode(&p, &word) == 0);
	CHECK(rtw88_net_ht20_snapshot_decode(word, &decoded) == 0);
	CHECK(p.generation == decoded.generation && p.rx_mcs == decoded.rx_mcs &&
	    p.tx_mcs == decoded.tx_mcs && p.ready == decoded.ready &&
	    p.ampdu == decoded.ampdu);
	CHECK(rtw88_net_ht20_snapshot_admit(word, &decoded) == 0);
	CHECK(rtw88_net_ht20_snapshot_admit(word, NULL) == EINVAL);
	CHECK(rtw88_net_ht20_snapshot_admit(0, &decoded) == EOPNOTSUPP);
	for (i = 0; i < sizeof(ready_bits) / sizeof(ready_bits[0]); i++) {
		struct rtw88_net_ht20_snapshot partial = p;
		uint8_t bit = ready_bits[i];
		CHECK(bit != 0 && (bit & (bit - 1)) == 0);
		CHECK((ready_mask & bit) == 0);
		ready_mask |= bit;
		partial.ready &= (uint8_t)~bit;
		CHECK(rtw88_net_ht20_snapshot_encode(&partial, &word) == 0);
		CHECK(rtw88_net_ht20_snapshot_admit(word, &decoded) == EOPNOTSUPP);
	}
	CHECK(ready_mask == RTW88_NET_HT20_READY_ALL);
	p.generation = 0;
	CHECK(rtw88_net_ht20_snapshot_encode(&p, &word) == EINVAL);
	p.generation = 1;
	p.rx_mcs = 0x1ff;
	CHECK(rtw88_net_ht20_snapshot_encode(&p, &word) == EINVAL);
	p.rx_mcs = 0xff;
	p.ready = 0x40;
	CHECK(rtw88_net_ht20_snapshot_encode(&p, &word) == EINVAL);
	p.ready = RTW88_NET_HT20_READY_ALL;
	p.ampdu = 0xe0;
	CHECK(rtw88_net_ht20_snapshot_encode(&p, &word) == EINVAL);
	CHECK(rtw88_net_ht20_snapshot_decode(0, &decoded) == ENOENT);
	CHECK(rtw88_net_ht20_snapshot_decode(UINT64_C(1) << 62, &decoded) == EINVAL);
	writer_error = pthread_create(&writer, NULL, ht20_snapshot_writer, &race);
	CHECK(writer_error == 0);
	if (writer_error != 0)
		return;
	reader_error = pthread_create(&reader, NULL, ht20_snapshot_reader, &race);
	CHECK(reader_error == 0);
	if (reader_error == 0) {
		CHECK(pthread_join(reader, NULL) == 0);
	}
	CHECK(pthread_join(writer, NULL) == 0);
	CHECK(race.writer_failures == 0 && race.reader_failures == 0);
	CHECK(race.reads != 0 || reader_error != 0);
}

int
main(void)
{
	struct rtw88_net_policy p = {0};
	unsigned char frame[64] = {0x08, 0x02};
	const uint8_t bssid[6] = {2, 1, 2, 3, 4, 5};
	uint64_t a, b, c;
	unsigned i;
	struct rtw88_net_state_diag diag = {
	    .state = 4, .error = EINVAL, .stage = 2, .channel = 6,
	    .core_channel = 6, .basic = 0, .slot = 9, .aid = 7,
	    .raw_aid = 0xc007, .bssid_flags = 1, .nrates = 12,
	    .flags = 0x200, .started = 1, .running = 1
	};
	char evidence[384], shortbuf[8];
	int evidence_len;

	ht20_snapshot_checks();

	/* Losing a bad input must not hide which strict call failed. */
	evidence_len = rtw88_net_state_format(evidence, sizeof(evidence), &diag);
	CHECK(evidence_len > 0 && (size_t)evidence_len < sizeof(evidence));
	CHECK(strstr(evidence, "state 4 failed: 22 stage 2") != NULL);
	CHECK(strstr(evidence, "channel 6 core_channel 6 basic 0") != NULL);
	CHECK(strstr(evidence, "slot 9 aid 7 raw_aid 0xc007") != NULL);
	CHECK(strstr(evidence, "bssid_flags 0x1 nrates 12 flags 0x200") != NULL);
	CHECK(strstr(evidence, "started 1 running 1") != NULL);
	diag.stage = 1;
	diag.channel = 166;
	diag.core_channel = 6;
	diag.basic = 0x150;
	diag.aid = 2008;
	diag.bssid_flags = 7;
	(void)rtw88_net_state_format(evidence, sizeof(evidence), &diag);
	CHECK(strstr(evidence, "failed: 22 stage 1 channel 166 core_channel 6") != NULL);
	CHECK(strstr(evidence, "basic 0x150 slot 9 aid 2008") != NULL);
	CHECK(strstr(evidence, "bssid_flags 0x7") != NULL);
	memset(shortbuf, 0x5a, sizeof(shortbuf));
	CHECK(rtw88_net_state_format(shortbuf, 7, &diag) > 7);
	CHECK(shortbuf[6] == '\0' && shortbuf[7] == 0x5a);
	/* Full-width invalid inputs still leave the last core fields visible. */
	memset(&diag, 0xff, sizeof(diag));
	evidence_len = rtw88_net_state_format(evidence, sizeof(evidence), &diag);
	CHECK(evidence_len > 0 && (size_t)evidence_len < sizeof(evidence));
	CHECK(strstr(evidence, "running 4294967295\n") != NULL);

	/* Beacon timeout configuration is in 1024-us TUs, not milliseconds. */
	CHECK(rtw88_net_tu_to_ms(0) == 0);
	CHECK(rtw88_net_tu_to_ms(1) == 2);
	CHECK(rtw88_net_tu_to_ms(100) == 103);
	CHECK(rtw88_net_tu_to_ms(700) == 717);
	CHECK(rtw88_net_tu_to_ms(65535) == 67108);
	/* Removing epoch/acceptance checks admits stale DMA after a rescan. */
	CHECK(rtw88_net_rx_admit(&p, 0, frame, 24, 0, 0) == ENXIO);
	a = rtw88_net_request(&p);
	CHECK(a != 0);
	CHECK(rtw88_net_commit(&p, a, 0, 1) == 0);
	CHECK(rtw88_net_rx_admit(&p, a, frame, 24, 0, 0) == 0);
	CHECK(p.queued == 1);
	rtw88_net_rx_release(&p);
	CHECK(p.queued == 0);
	rtw88_net_rx_release(&p);
	CHECK(p.queued == 0);
	b = rtw88_net_request(&p);
	CHECK(b != a);
	CHECK(rtw88_net_commit(&p, a, 0, 1) == ECANCELED);
	CHECK(rtw88_net_rx_admit(&p, b, frame, 24, 0, 0) == ENXIO);
	CHECK(rtw88_net_commit(&p, b, EIO, 1) == EIO);
	CHECK(!p.accepting);
	CHECK(rtw88_net_commit(&p, b, 0, 1) == 0);
	CHECK(rtw88_net_rx_admit(&p, a, frame, 24, 0, 0) == ECANCELED);
	/* BAR ingress needs a published authorized session and exact wire shape. */
	struct rtw88_net_policy before_bar=p;uint64_t before_bar_epoch=b;
	uint8_t bar[20] = {0x84};bar[4]=2;bar[10]=4;bar[16]=4;
	p.rxba_mask=1;p.rxba_ra[0]=2;p.rxba_ta[0]=4;
	CHECK(rtw88_net_rx_admit(&p,b,bar,20,0,0)==0);
	rtw88_net_rx_release(&p);
	bar[10]^=2;CHECK(rtw88_net_rx_admit(&p,b,bar,20,0,0)!=0);bar[10]^=2;
	bar[4]^=2;CHECK(rtw88_net_rx_admit(&p,b,bar,20,0,0)!=0);bar[4]^=2;
	bar[17]=0x10;CHECK(rtw88_net_rx_admit(&p,b,bar,20,0,0)!=0);bar[17]=0;
	bar[16]=0;CHECK(rtw88_net_rx_admit(&p,b,bar,20,0,0)!=0);bar[16]=4;
	bar[18]=1;CHECK(rtw88_net_rx_admit(&p,b,bar,20,0,0)!=0);bar[18]=0;
	bar[1]=0x40;CHECK(rtw88_net_rx_admit(&p,b,bar,20,0,0)!=0);bar[1]=0;
	CHECK(rtw88_net_rx_admit(&p,b,bar,20,1,0)!=0);
	CHECK(rtw88_net_rx_admit(&p,b,bar,20,0,3)!=0);
	CHECK(rtw88_net_rx_admit(&p,a,bar,20,0,0)!=0);
	p.rxba_mask=0;CHECK(rtw88_net_rx_admit(&p,b,bar,20,0,0)!=0);
	p.rxba_mask=1;b=rtw88_net_request(&p);CHECK(p.rxba_mask==0);
	CHECK(rtw88_net_commit(&p,b,0,1)==0);
	p=before_bar;b=before_bar_epoch;
	/* Hardware-decrypted input must not masquerade as software ciphertext. */
	frame[1] = 0x42;
	CHECK(rtw88_net_rx_admit(&p, b, frame, 40, 0, 3) == EACCES);
	CHECK(rtw88_net_rx_admit(&p, b, frame, 40, 8, 3) == 0);
	rtw88_net_rx_release(&p);
	/* Bounds, version, errors and four-address frames never reach the stack. */
	CHECK(rtw88_net_rx_admit(&p, b, NULL, 40, 8, 3) == EINVAL);
	CHECK(rtw88_net_rx_admit(&p, b, frame, 23, 8, 3) == EMSGSIZE);
	CHECK(rtw88_net_rx_admit(&p, b, frame, 4097, 8, 3) == EMSGSIZE);
	CHECK(rtw88_net_rx_admit(&p, b, frame, 40, 1, 3) == EBADMSG);
	CHECK(rtw88_net_rx_admit(&p, b, frame, 40, 2, 3) == EBADMSG);
	CHECK(rtw88_net_rx_admit(&p, b, frame, 40, 16, 0) == EBADMSG);
	frame[0] = 0x09;
	CHECK(rtw88_net_rx_admit(&p, b, frame, 40, 8, 3) == EPROTONOSUPPORT);
	frame[0] = 0x08;
	frame[1] = 3;
	CHECK(rtw88_net_rx_admit(&p, b, frame, 40, 0, 0) == EPROTONOSUPPORT);
	frame[1] = 2;
	/* The 65th queued mbuf must not grow the ingress backlog. */
	for (i = 0; i < 64; i++)
		CHECK(rtw88_net_rx_admit(&p, b, frame, 24, 0, 0) == 0);
	CHECK(rtw88_net_rx_admit(&p, b, frame, 24, 0, 0) == ENOBUFS);
	CHECK(p.queued == 64);
	/* Real admission refusals must retain a distinct diagnostic reason. */
	CHECK(p.rx.accepted == 66 && p.rx.inactive == 2 && p.rx.epoch == 1);
	CHECK(p.rx.invalid == 1 && p.rx.size == 2 && p.rx.corrupt == 3);
	CHECK(p.rx.format == 2 && p.rx.crypto == 1 && p.rx.full == 1);
	/* A retune between admission and softint delivery has a separate cause. */
	CHECK(rtw88_net_rx_ready(&p, b));
	CHECK(!rtw88_net_rx_ready(&p, a));
	CHECK(p.rx.delivered == 1 && p.rx.delivery_epoch == 1);
	struct rtw88_net_policy closed = {0};
	CHECK(!rtw88_net_rx_ready(&closed, 0));
	CHECK(closed.rx.delivery_inactive == 1 && closed.rx.delivered == 0);
	/* A live generation starts its watchdog at RUN, before its first beacon. */
	CHECK(rtw88_net_beacon_start(&p, b, 100) == 0);
	CHECK(!rtw88_net_beacon_poll(&p, b, 599, 500));
	CHECK(rtw88_net_beacon_poll(&p, b, 600, 500));
	CHECK(!rtw88_net_beacon_poll(&p, b, 600, 500));
	CHECK(!rtw88_net_beacon_poll(&p, b, 1099, 500));
	/* Other BSSIDs, probe responses and truncated beacons cannot keep RUN live. */
	frame[0] = 0x80;
	frame[1] = 0;
	CHECK(rtw88_net_beacon_start(&p, b, 100) == 0);
	CHECK(rtw88_net_beacon_note(&p, b, 400, frame, 36, bssid) == ENOENT);
	CHECK(rtw88_net_beacon_poll(&p, b, 600, 500));
	memcpy(frame + 16, bssid, 6);
	CHECK(rtw88_net_beacon_start(&p, b, 100) == 0);
	CHECK(rtw88_net_beacon_note(&p, b, 400, frame, 35, bssid) == ENOENT);
	CHECK(rtw88_net_beacon_poll(&p, b, 600, 500));
	frame[0] = 0x50;
	CHECK(rtw88_net_beacon_start(&p, b, 100) == 0);
	CHECK(rtw88_net_beacon_note(&p, b, 400, frame, 36, bssid) == ENOENT);
	CHECK(rtw88_net_beacon_poll(&p, b, 600, 500));
	frame[0] = 0x80;
	CHECK(rtw88_net_beacon_note(&p, b, 1200, frame, 36, bssid) == 0);
	CHECK(!rtw88_net_beacon_poll(&p, b, 1699, 500));
	CHECK(rtw88_net_beacon_poll(&p, b, 1700, 500));
	/* State changes invalidate old beacons even if a new RUN starts quickly. */
	c = rtw88_net_request(&p);
	CHECK(!rtw88_net_beacon_poll(&p, b, 2200, 500));
	CHECK(rtw88_net_commit(&p, c, 0, 1) == 0);
	CHECK(rtw88_net_beacon_note(&p, b, 2200, frame, 36, bssid) == ECANCELED);
	CHECK(rtw88_net_beacon_note(&p, c, 2200, frame, 36, bssid) == ENXIO);
	/* Unsigned elapsed ticks handle wrap, and do not fire on backward time. */
	CHECK(rtw88_net_beacon_start(&p, c, UINT32_MAX - 50) == 0);
	CHECK(!rtw88_net_beacon_poll(&p, c, 48, 100));
	CHECK(rtw88_net_beacon_poll(&p, c, 49, 100));
	CHECK(!rtw88_net_beacon_poll(&p, c, 49, 100));
	CHECK(!rtw88_net_beacon_poll(&p, c, 48, 100));
	CHECK(!rtw88_net_beacon_poll(&p, c, 1000, 0));
	CHECK(!rtw88_net_beacon_poll(&p, c, 1000, UINT32_MAX));
	rtw88_net_close(&p);
	CHECK(!p.accepting && p.dying);
	CHECK(rtw88_net_commit(&p, b, 0, 1) == ECANCELED);
	CHECK(rtw88_net_rx_admit(&p, p.epoch, frame, 24, 0, 0) == ENXIO);
	CHECK(!rtw88_net_beacon_poll(&p, c, 1000, 100));
	CHECK(rtw88_net_beacon_start(&p, p.epoch, 1000) == ENXIO);
	printf("rtw88 net policy: %u assertions, %u failures\n", checks, failures);
	return failures != 0;
}
