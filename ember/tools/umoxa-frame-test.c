/* Origin: EmberBSD MOXA G2 framing contract tests, 2026-10-09. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "umoxa_frame.h"

#define CHECK(x) do { \
	if (!(x)) { \
		fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #x); \
		return 0; \
	} \
} while (0)

struct capture {
	unsigned int count;
	uint16_t port[8], code[8];
	size_t length[8];
	uint8_t bytes[8][8];
};

static void
data_capture(void *arg, uint16_t port, const uint8_t *data, size_t len)
{
	struct capture *c = arg;
	unsigned int n = c->count++;

	if (n < 8 && len <= 8) {
		c->port[n] = port;
		c->length[n] = len;
		memcpy(c->bytes[n], data, len);
	}
}

static void
event_capture(void *arg, uint16_t port, uint16_t code, const uint8_t *data)
{
	struct capture *c = arg;
	unsigned int n = c->count++;

	if (n < 8) {
		c->port[n] = port;
		c->code[n] = code;
		memcpy(c->bytes[n], data, 4);
	}
}

static int
test_data_wire_order(void)
{
	const uint8_t good[] = {0, 0, 0, 1, 'A', 0, 3, 0, 2, 'B', 'C'};
	struct capture c = {0};

	CHECK(umoxa_g2_rx_data(good, sizeof(good), 65535,
	    data_capture, &c) == 0);
	CHECK(c.count == 2 && c.port[0] == 0 && c.port[1] == 3);
	CHECK(c.length[0] == 1 && c.length[1] == 2);
	CHECK(c.bytes[0][0] == 'A' && memcmp(c.bytes[1], "BC", 2) == 0);
	return 1;
}

static int
test_bad_suffix_atomic(void)
{
	const uint8_t bad[] = {0, 0, 0, 1, 'A', 0, 2, 0, 2, 'X'};
	uint8_t frame[] = {0, 0, 0, 1, 'A', 0, 3, 0, 1, 'B'};
	struct capture c = {0};
	size_t n;

	CHECK(umoxa_g2_rx_data(bad, sizeof(bad), 65535,
	    data_capture, &c) == EPROTO && c.count == 0);
	for (n = 1; n < 5; n++) {
		CHECK(umoxa_g2_rx_data(frame, n, 65535,
		    data_capture, &c) == EPROTO && c.count == 0);
		CHECK(umoxa_g2_rx_data(frame, 5 + n, 65535,
		    data_capture, &c) == EPROTO && c.count == 0);
	}
	frame[6] = 4;
	CHECK(umoxa_g2_rx_data(frame, sizeof(frame), 65535,
	    data_capture, &c) == EPROTO && c.count == 0);
	frame[5] = 0x80;
	frame[6] = 0;
	CHECK(umoxa_g2_rx_data(frame, sizeof(frame), 65535,
	    data_capture, &c) == EPROTO && c.count == 0);
	CHECK(umoxa_g2_rx_data(bad, sizeof(bad), 1,
	    data_capture, &c) == EPROTO && c.count == 0);
	return 1;
}

static int
test_zero_record(void)
{
	const uint8_t frame[] = {0, 2, 0, 0, 0, 3, 0, 1, 'Z'};
	struct capture c = {0};

	CHECK(umoxa_g2_rx_data(NULL, 0, 1, data_capture, &c) == 0);
	CHECK(umoxa_g2_rx_data(frame, 4, 1, data_capture, &c) == 0);
	CHECK(c.count == 0);
	CHECK(umoxa_g2_rx_data(frame, sizeof(frame), 1,
	    data_capture, &c) == 0);
	CHECK(c.count == 1 && c.port[0] == 3 && c.bytes[0][0] == 'Z');
	return 1;
}

static int
test_event_opaque(void)
{
	uint8_t events[] = {0, 2, 0, 2, 0, 0, 0, 0,
	    0, 3, 0xff, 0xff, 0xde, 0xad, 0xbe, 0xef};
	struct capture c = {0};
	size_t n;

	CHECK(umoxa_g2_rx_events(NULL, 0, event_capture, &c) == 0);
	for (n = 1; n < 8; n++) {
		CHECK(umoxa_g2_rx_events(events, n,
		    event_capture, &c) == EPROTO && c.count == 0);
		CHECK(umoxa_g2_rx_events(events, 8 + n,
		    event_capture, &c) == EPROTO && c.count == 0);
	}
	events[9] = 4;
	CHECK(umoxa_g2_rx_events(events, sizeof(events),
	    event_capture, &c) == EPROTO && c.count == 0);
	events[9] = 3;
	CHECK(umoxa_g2_rx_events(events, sizeof(events), event_capture, &c) == 0);
	CHECK(c.count == 2 && c.port[0] == 2 && c.code[0] == 2);
	CHECK(c.port[1] == 3 && c.code[1] == 65535);
	CHECK(memcmp(c.bytes[1], events + 12, 4) == 0);
	return 1;
}

static int
test_tx_send_next(void)
{
	const uint8_t data[] = {0xaa, 0xbb};
	const uint8_t expected[] = {0x80, 3, 0, 2, 0xaa, 0xbb};
	uint8_t out[6];
	size_t written = 99;
	unsigned int port;

	CHECK(umoxa_g2_tx(3, data, 2, 1, out, sizeof(out), &written) == 0);
	CHECK(written == 6 && memcmp(out, expected, 6) == 0);
	for (port = 0; port < 4; port++) {
		CHECK(umoxa_g2_tx(port, data, 2, 0,
		    out, sizeof(out), &written) == 0);
		CHECK(out[0] == 0 && out[1] == port && written == 6);
	}
	return 1;
}

static int
test_capacity_unchanged(void)
{
	const uint8_t data[] = {0xaa, 0xbb};
	uint8_t out[6], original[6];
	size_t cap, written;

	memset(out, 0x5a, sizeof(out));
	memcpy(original, out, sizeof(out));
	for (cap = 0; cap < 6; cap++) {
		written = 99;
		CHECK(umoxa_g2_tx(3, data, 2, 1, out, cap,
		    &written) == EMSGSIZE);
		CHECK(written == 0 && memcmp(out, original, sizeof(out)) == 0);
	}
	return 1;
}

static int
test_overlap(void)
{
	const uint8_t expected[] = {0x80, 3, 0, 2, 0xaa, 0xbb};
	uint8_t out[10];
	size_t offset, written;

	for (offset = 0; offset <= 8; offset++) {
		memset(out, 0, sizeof(out));
		out[offset] = 0xaa;
		out[offset + 1] = 0xbb;
		CHECK(umoxa_g2_tx(3, out + offset, 2, 1,
		    out, 6, &written) == 0);
		CHECK(written == 6 && memcmp(out, expected, 6) == 0);
	}
	return 1;
}

static int
test_invalid_domains(void)
{
	uint8_t byte = 0xaa, out[6], original[6], frame[4] = {0xff, 0xff, 0, 0};
	struct capture c = {0};
	size_t written = 99;

	CHECK(umoxa_g2_rx_data(NULL, 1, 1, data_capture, &c) == EINVAL);
	CHECK(umoxa_g2_rx_data(NULL, 0, 0, data_capture, &c) == EINVAL);
	CHECK(umoxa_g2_rx_data(NULL, 0, 65536, data_capture, &c) == EINVAL);
	CHECK(umoxa_g2_rx_data(NULL, 0, 1, NULL, &c) == EINVAL);
	CHECK(umoxa_g2_rx_events(NULL, 1, event_capture, &c) == EINVAL);
	CHECK(umoxa_g2_rx_events(NULL, 0, NULL, &c) == EINVAL);
	CHECK(umoxa_g2_rx_data(frame, 4, 1, data_capture, &c) == EPROTO);
	CHECK(c.count == 0);
	memset(out, 0x5a, sizeof(out));
	memcpy(original, out, sizeof(out));
#define BAD_TX(p, d, n, sn, dst, result) do { \
	written = 99; \
	CHECK(umoxa_g2_tx(p, d, n, sn, dst, 6, &written) == result); \
	CHECK(written == 0 && memcmp(out, original, sizeof(out)) == 0); \
} while (0)
	BAD_TX(4, &byte, 1, 0, out, EINVAL);
	BAD_TX(65535, &byte, 1, 0, out, EINVAL);
	BAD_TX(0, NULL, 1, 0, out, EINVAL);
	BAD_TX(0, &byte, 0, 0, out, EINVAL);
	BAD_TX(0, &byte, 65536, 0, out, EINVAL);
	BAD_TX(0, &byte, SIZE_MAX, 0, out, EINVAL);
	BAD_TX(0, &byte, 1, -1, out, EINVAL);
	BAD_TX(0, &byte, 1, 2, out, EINVAL);
	BAD_TX(0, &byte, 1, 0, NULL, EINVAL);
#undef BAD_TX
	CHECK(umoxa_g2_tx(0, &byte, 1, 0, out, 6, NULL) == EINVAL);
	CHECK(memcmp(out, original, sizeof(out)) == 0);
	return 1;
}

int
main(void)
{
	const struct { const char *name; int (*run)(void); } tests[] = {
		{"data_wire_order", test_data_wire_order},
		{"bad_suffix_atomic", test_bad_suffix_atomic},
		{"zero_record", test_zero_record},
		{"event_opaque", test_event_opaque},
		{"tx_send_next", test_tx_send_next},
		{"capacity_unchanged", test_capacity_unchanged},
		{"overlap", test_overlap},
		{"invalid_domains", test_invalid_domains}
	};
	unsigned int i, passed = 0;
	int ok;

	for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
		ok = tests[i].run();
		printf("%s %s\n", ok ? "PASS" : "FAIL", tests[i].name);
		passed += ok;
	}
	printf("umoxa framing: %u/8 groups passed, %u failures\n", passed, 8 - passed);
	return passed == 8 ? 0 : 1;
}
