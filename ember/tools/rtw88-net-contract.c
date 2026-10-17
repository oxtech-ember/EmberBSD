/* SPDX-License-Identifier: BSD-2-Clause */
/* Original native RTL8822CE net ingress contracts, AI-assisted. */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "rtw88_net_policy.h"

static unsigned checks, failures;
#define CHECK(e) do { checks++; if (!(e)) { failures++; \
	fprintf(stderr, "line %u: %s\n", __LINE__, #e); } } while (0)

int
main(void)
{
	struct rtw88_net_policy p = {0};
	unsigned char frame[64] = {0x08, 0x02};
	const uint8_t bssid[6] = {2, 1, 2, 3, 4, 5};
	uint64_t a, b, c;
	unsigned i;

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
