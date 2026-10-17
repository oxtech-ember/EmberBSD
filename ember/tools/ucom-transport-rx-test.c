/* Origin: EmberBSD bounded ucom RX contracts, 2026-10-10. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "ucom_transport_rx.h"

#define CHECK(c) do { if (!(c)) { \
	fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #c); return 1; \
} } while (0)

static int
test_empty_invalid(void)
{
	struct ucom_transport_rx q;
	size_t len;

	ucom_transport_rx_init(&q);
	CHECK(q.used == 0 && q.paused == 0);
	CHECK(ucom_transport_rx_peek(&q, &len) == NULL && len == 0);
	CHECK(ucom_transport_rx_put(&q, NULL, 0) == 0);
	CHECK(ucom_transport_rx_put(&q, NULL, 1) == EINVAL);
	CHECK(ucom_transport_rx_put(NULL, "A", 1) == EINVAL);
	CHECK(ucom_transport_rx_drop(&q, 1) == EINVAL && q.used == 0);
	return 0;
}

static int
test_fifo_wrap(void)
{
	struct ucom_transport_rx q;
	uint8_t a[6000], b[6000];
	const uint8_t *p;
	size_t len;

	memset(a, 'A', sizeof(a));
	memset(b, 'B', sizeof(b));
	ucom_transport_rx_init(&q);
	CHECK(ucom_transport_rx_put(&q, a, sizeof(a)) == 0);
	CHECK(ucom_transport_rx_drop(&q, 5000) == 0);
	CHECK(ucom_transport_rx_put(&q, b, sizeof(b)) == 0);
	CHECK(q.used == 7000);
	for (size_t i = 0; i < 7000; i++) {
		p = ucom_transport_rx_peek(&q, &len);
		CHECK(p != NULL && len > 0 && *p == (i < 1000 ? 'A' : 'B'));
		CHECK(ucom_transport_rx_drop(&q, 1) == 0);
	}
	CHECK(q.used == 0 && q.paused == 0);
	return 0;
}

static int
test_exact_capacity(void)
{
	struct ucom_transport_rx q;
	uint8_t data[UCOM_TRANSPORT_RX_CAPACITY];
	const uint8_t *p;
	size_t len;

	for (size_t i = 0; i < sizeof(data); i++)
		data[i] = (uint8_t)i;
	ucom_transport_rx_init(&q);
	CHECK(ucom_transport_rx_put(&q, data, sizeof(data)) == 0);
	p = ucom_transport_rx_peek(&q, &len);
	CHECK(len == sizeof(data) && memcmp(p, data, len) == 0);
	CHECK(q.paused == 1);
	CHECK(ucom_transport_rx_drop(&q, len) == 0 && q.used == 0);
	return 0;
}

static int
test_overflow_atomic(void)
{
	struct ucom_transport_rx q, before;
	uint8_t data[UCOM_TRANSPORT_RX_CAPACITY + 1];

	memset(data, 0xa5, sizeof(data));
	ucom_transport_rx_init(&q);
	before = q;
	CHECK(ucom_transport_rx_put(&q, data, sizeof(data)) == ENOBUFS);
	CHECK(memcmp(&q, &before, sizeof(q)) == 0);
	CHECK(ucom_transport_rx_put(&q, data, 8191) == 0);
	before = q;
	CHECK(ucom_transport_rx_put(&q, data, 2) == ENOBUFS);
	CHECK(memcmp(&q, &before, sizeof(q)) == 0);
	return 0;
}

static int
test_hysteresis(void)
{
	struct ucom_transport_rx q;
	uint8_t data[6144];

	memset(data, 1, sizeof(data));
	ucom_transport_rx_init(&q);
	CHECK(ucom_transport_rx_put(&q, data, 6143) == 0 && !q.paused);
	CHECK(ucom_transport_rx_put(&q, data, 1) == 0 && q.paused);
	CHECK(ucom_transport_rx_drop(&q, 4095) == 0 && q.paused);
	CHECK(q.used == 2049);
	CHECK(ucom_transport_rx_drop(&q, 1) == 0 && !q.paused);
	CHECK(q.used == 2048);
	return 0;
}

static int
test_four_port_isolation(void)
{
	struct ucom_transport_rx q[4];
	uint8_t full[8192];
	size_t len;

	memset(full, 3, sizeof(full));
	for (size_t i = 0; i < 4; i++) {
		ucom_transport_rx_init(&q[i]);
		CHECK(ucom_transport_rx_put(&q[i], &full[i], 1) == 0);
	}
	CHECK(ucom_transport_rx_put(&q[0], full, sizeof(full)) == ENOBUFS);
	for (size_t i = 1; i < 4; i++) {
		CHECK(q[i].used == 1 && !q[i].paused);
		CHECK(*ucom_transport_rx_peek(&q[i], &len) == 3 && len == 1);
	}
	return 0;
}

int
main(void)
{
	struct { const char *name; int (*test)(void); } tests[] = {
		{ "empty_invalid", test_empty_invalid },
		{ "fifo_wrap", test_fifo_wrap },
		{ "exact_capacity", test_exact_capacity },
		{ "overflow_atomic", test_overflow_atomic },
		{ "hysteresis", test_hysteresis },
		{ "four_port_isolation", test_four_port_isolation }
	};
	int failed = 0;

	for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
		int error = tests[i].test();
		printf("%s %s\n", error ? "FAIL" : "PASS", tests[i].name);
		failed += error != 0;
	}
	printf("ucom RX: %d/6 groups passed, %d failures\n", 6 - failed, failed);
	return failed ? 1 : 0;
}
