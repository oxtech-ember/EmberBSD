/* Origin: EmberBSD shared MOXA G2 TX queue contracts, 2026-10-10. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "umoxa_g2_txq.h"

#define CHECK(e) do { if (!(e)) { \
	fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #e); \
	return 1; } } while (0)

static uint8_t payload[UMOXA_G2_PAYLOAD_MAX];
static const uint8_t credit0[8] = { 0, 0, 0, 2, 0, 0, 0, 0 };

static int
setup(struct umoxa_g2_txq *q)
{
	unsigned int p;

	CHECK(umoxa_g2_txq_init(q, 1024, 100) == 0);
	for (p = 0; p < 4; p++)
		CHECK(umoxa_g2_txq_open(q, p, 1) == 0);
	return 0;
}

static int
send_done(struct umoxa_g2_txq *q, unsigned int port, uint64_t cookie,
    size_t len, uint64_t now, int flag)
{
	struct umoxa_g2_xfer x;
	struct umoxa_g2_result r;

	CHECK(umoxa_g2_txq_submit(q, port, 1, cookie, payload, len) == 0);
	CHECK(umoxa_g2_txq_pick(q, now, &x) == 0);
	CHECK(x.port == port && x.len == len + 4);
	CHECK(x.data[0] == (flag ? 0x80 : 0) && x.data[1] == port);
	CHECK(umoxa_g2_txq_done(q, x.token, x.len, 0, &r) == 1);
	CHECK(r.deliver && r.actual == len && r.error == 0);
	return 0;
}

static int
init_invalid(void)
{
	struct umoxa_g2_txq q, saved;
	struct umoxa_g2_xfer x;
	uint32_t mask;

	memset(&q, 0x5a, sizeof(q));
	saved = q;
	CHECK(umoxa_g2_txq_init(&q, 4, 100) == EINVAL);
	CHECK(memcmp(&q, &saved, sizeof(q)) == 0);
	CHECK(umoxa_g2_txq_init(&q, 1025, 100) == EINVAL);
	CHECK(umoxa_g2_txq_init(&q, 5, 0) == EINVAL);
	CHECK(umoxa_g2_txq_init(NULL, 5, 100) == EINVAL);
	CHECK(umoxa_g2_txq_init(&q, 5, 100) == 0);
	CHECK(umoxa_g2_txq_open(&q, 4, 1) == EINVAL);
	CHECK(umoxa_g2_txq_open(&q, 0, 0) == EINVAL);
	CHECK(umoxa_g2_txq_close(&q, 4) == EINVAL);
	CHECK(umoxa_g2_txq_submit(&q, 0, 1, 1, payload, 1) == ENXIO);
	CHECK(umoxa_g2_txq_pick(&q, 0, NULL) == EINVAL);
	CHECK(umoxa_g2_txq_pick(&q, 0, &x) == EAGAIN);
	CHECK(x.token == 0 && x.data == NULL);
	CHECK(umoxa_g2_txq_tick(&q, 0, &mask) == 0 && mask == 0);
	return 0;
}

static int
copied_bounded_queue(void)
{
	struct umoxa_g2_txq q, saved;
	struct umoxa_g2_xfer x;
	struct umoxa_g2_result r;
	uint8_t buf[1020];

	CHECK(setup(&q) == 0);
	memset(buf, 0xab, sizeof(buf));
	CHECK(umoxa_g2_txq_submit(&q, 0, 1, 1, buf, sizeof(buf)) == 0);
	memset(buf, 0xcd, sizeof(buf));
	saved = q;
	CHECK(umoxa_g2_txq_submit(&q, 0, 1, 2, buf, 1) == EBUSY);
	CHECK(umoxa_g2_txq_submit(&q, 1, 1, 1, buf, 1021) == EMSGSIZE);
	CHECK(umoxa_g2_txq_submit(&q, 1, 1, 0, buf, 1) == EINVAL);
	CHECK(umoxa_g2_txq_submit(&q, 1, 1, 1, NULL, 1) == EINVAL);
	CHECK(umoxa_g2_txq_submit(&q, 1, 1, 1, buf, 0) == EINVAL);
	CHECK(umoxa_g2_txq_submit(&q, 1, 2, 1, buf, 1) == ESTALE);
	CHECK(memcmp(&q, &saved, sizeof(q)) == 0);
	CHECK(umoxa_g2_txq_pick(&q, 1, &x) == 0);
	CHECK(x.len == 1024 && x.data[2] == 3 && x.data[3] == 0xfc);
	CHECK(x.data[4] == 0xab && x.data[1023] == 0xab);
	CHECK(umoxa_g2_txq_done(&q, x.token, x.len, 0, &r) == 1);
	CHECK(umoxa_g2_txq_submit(&q, 0, 1, 1, buf, 1) == EALREADY);
	return 0;
}

static int
round_robin(void)
{
	struct umoxa_g2_txq q;
	struct umoxa_g2_xfer x, busy;
	struct umoxa_g2_result r;
	unsigned int p, round;

	CHECK(setup(&q) == 0);
	for (round = 1; round <= 3; round++) {
		for (p = 0; p < 4; p++)
			CHECK(umoxa_g2_txq_submit(&q, p, 1, round,
			    payload, p + 1) == 0);
		for (p = 0; p < 4; p++) {
			CHECK(umoxa_g2_txq_pick(&q, round, &x) == 0);
			CHECK(x.port == p && x.epoch == 1 && x.cookie == round);
			CHECK(umoxa_g2_txq_pick(&q, round, &busy) == EBUSY);
			CHECK(busy.token == 0);
			CHECK(umoxa_g2_txq_done(&q, x.token, x.len, 0, &r) == 1);
			CHECK(r.deliver && r.actual == p + 1);
		}
	}
	return 0;
}

static int
send_next_order(void)
{
	struct umoxa_g2_txq q;
	struct umoxa_g2_xfer x;
	uint32_t mask;

	CHECK(setup(&q) == 0);
	CHECK(send_done(&q, 0, 1, 511, 1, 0) == 0);
	CHECK(send_done(&q, 0, 2, 1, 2, 0) == 0);
	CHECK(send_done(&q, 0, 3, 1, 3, 1) == 0);
	CHECK(umoxa_g2_txq_submit(&q, 0, 1, 4, payload, 1) == 0);
	CHECK(umoxa_g2_txq_pick(&q, 4, &x) == EAGAIN);
	CHECK(umoxa_g2_txq_events(&q, credit0, 8, &mask) == 0 && mask == 0);
	CHECK(umoxa_g2_txq_pick(&q, 5, &x) == 0 && x.data[0] == 0);
	return 0;
}

static int
early_duplicate_events(void)
{
	struct umoxa_g2_txq q;
	struct umoxa_g2_xfer x;
	struct umoxa_g2_result r;
	uint32_t mask;
	uint8_t other[8] = { 0, 0, 0, 3, 0, 0, 0, 0 };

	CHECK(setup(&q) == 0);
	CHECK(umoxa_g2_txq_events(&q, credit0, 8, &mask) == 0);
	CHECK(send_done(&q, 0, 1, 600, 1, 0) == 0);
	CHECK(umoxa_g2_txq_submit(&q, 0, 1, 2, payload, 2) == 0);
	CHECK(umoxa_g2_txq_pick(&q, 2, &x) == 0 && x.data[0] == 0x80);
	CHECK(umoxa_g2_txq_events(&q, other, 8, &mask) == 0);
	CHECK(q.ports[0].waiting);
	CHECK(umoxa_g2_txq_events(&q, credit0, 8, &mask) == 0);
	CHECK(umoxa_g2_txq_events(&q, credit0, 8, &mask) == 0);
	CHECK(!q.ports[0].waiting && q.ports[0].sent == 0);
	CHECK(umoxa_g2_txq_done(&q, x.token, x.len, 0, &r) == 1);
	CHECK(send_done(&q, 0, 3, 1, 3, 0) == 0);
	return 0;
}

static int
local_timeout(void)
{
	struct umoxa_g2_txq q;
	struct umoxa_g2_xfer x;
	uint32_t mask;

	CHECK(setup(&q) == 0);
	CHECK(send_done(&q, 0, 1, 600, 1, 0) == 0);
	CHECK(send_done(&q, 0, 2, 1, 2, 1) == 0);
	CHECK(umoxa_g2_txq_submit(&q, 0, 1, 3, payload, 1) == 0);
	CHECK(send_done(&q, 1, 1, 1, 3, 0) == 0);
	CHECK(umoxa_g2_txq_tick(&q, 101, &mask) == 0 && mask == 0);
	CHECK(umoxa_g2_txq_tick(&q, 102, &mask) == 0 && mask == 1);
	CHECK(umoxa_g2_txq_submit(&q, 0, 1, 4, payload, 1) == ETIMEDOUT);
	CHECK(send_done(&q, 1, 2, 1, 103, 0) == 0);
	CHECK(umoxa_g2_txq_pick(&q, 103, &x) == EAGAIN);
	CHECK(umoxa_g2_txq_tick(&q, 104, &mask) == 0 && mask == 0);
	return 0;
}

static int
shared_failure_watchdog(void)
{
	struct umoxa_g2_txq q;
	struct umoxa_g2_xfer x;
	struct umoxa_g2_result r;
	uint8_t saved[1024];
	uint32_t mask;

	CHECK(setup(&q) == 0);
	CHECK(umoxa_g2_txq_submit(&q, 0, 1, 1, payload, 10) == 0);
	CHECK(umoxa_g2_txq_pick(&q, 1, &x) == 0);
	CHECK(umoxa_g2_txq_done(&q, x.token, x.len - 1, 0, &r) == 1);
	CHECK(r.deliver && r.error == EIO && r.fault_mask == 1);
	CHECK(send_done(&q, 1, 1, 1, 2, 0) == 0);
	CHECK(umoxa_g2_txq_submit(&q, 1, 1, 2, payload, 10) == 0);
	CHECK(umoxa_g2_txq_pick(&q, 3, &x) == 0);
	CHECK(umoxa_g2_txq_done(&q, x.token, x.len + 1, 0, &r) == 1);
	CHECK(r.error == EPROTO && r.fault_mask == 2);
	CHECK(setup(&q) == 0);
	CHECK(umoxa_g2_txq_submit(&q, 2, 1, 1, payload, 10) == 0);
	CHECK(umoxa_g2_txq_pick(&q, 5, &x) == 0);
	memcpy(saved, x.data, x.len);
	CHECK(umoxa_g2_txq_tick(&q, 105, &mask) == 0 && mask == 15);
	CHECK(q.active && memcmp(saved, x.data, x.len) == 0);
	CHECK(umoxa_g2_txq_done(&q, x.token, x.len, 0, &r) == 1);
	CHECK(!r.deliver);
	CHECK(setup(&q) == 0);
	CHECK(umoxa_g2_txq_submit(&q, 3, 1, 1, payload, 1) == 0);
	CHECK(umoxa_g2_txq_pick(&q, 1, &x) == 0);
	CHECK(umoxa_g2_txq_done(&q, x.token, 0, ENODEV, &r) == 1);
	CHECK(r.deliver && r.error == ENODEV && r.fault_mask == 15);
	return 0;
}

static int
close_reopen_barrier(void)
{
	struct umoxa_g2_txq q;
	struct umoxa_g2_xfer x, next;
	struct umoxa_g2_result r;
	uint8_t saved[1024];

	CHECK(setup(&q) == 0);
	CHECK(umoxa_g2_txq_submit(&q, 0, 1, 1, payload, 10) == 0);
	CHECK(umoxa_g2_txq_pick(&q, 1, &x) == 0);
	memcpy(saved, x.data, x.len);
	CHECK(umoxa_g2_txq_close(&q, 0) == 0);
	CHECK(umoxa_g2_txq_open(&q, 0, 2) == EBUSY);
	CHECK(memcmp(saved, x.data, x.len) == 0);
	CHECK(umoxa_g2_txq_submit(&q, 1, 1, 1, payload, 1) == 0);
	CHECK(umoxa_g2_txq_pick(&q, 2, &next) == EBUSY);
	CHECK(umoxa_g2_txq_done(&q, x.token, 0, 0, &r) == 1 && !r.deliver);
	CHECK(umoxa_g2_txq_open(&q, 0, 1) == ESTALE);
	CHECK(umoxa_g2_txq_open(&q, 0, 2) == 0);
	CHECK(umoxa_g2_txq_submit(&q, 0, 1, 1, payload, 1) == ESTALE);
	CHECK(umoxa_g2_txq_pick(&q, 3, &next) == 0 && next.port == 1);
	return 0;
}

static int
stale_done(void)
{
	struct umoxa_g2_txq q, saved;
	struct umoxa_g2_xfer old, next;
	struct umoxa_g2_result r;

	CHECK(setup(&q) == 0);
	CHECK(umoxa_g2_txq_submit(&q, 0, 1, 1, payload, 1) == 0);
	CHECK(umoxa_g2_txq_pick(&q, 1, &old) == 0);
	CHECK(umoxa_g2_txq_done(&q, old.token, old.len, 0, &r) == 1);
	CHECK(umoxa_g2_txq_submit(&q, 1, 1, 1, payload, 1) == 0);
	CHECK(umoxa_g2_txq_pick(&q, 2, &next) == 0);
	CHECK(next.token > old.token);
	saved = q;
	memset(&r, 0xff, sizeof(r));
	CHECK(umoxa_g2_txq_done(&q, old.token, old.len, EIO, &r) == 0);
	CHECK(!r.deliver && r.fault_mask == 0);
	CHECK(umoxa_g2_txq_done(&q, 0, 0, 0, &r) == 0);
	CHECK(memcmp(&q, &saved, sizeof(q)) == 0);
	CHECK(umoxa_g2_txq_done(&q, next.token, next.len, 0, &r) == 1);
	CHECK(r.deliver && r.port == 1);
	return 0;
}

static int
malformed_events(void)
{
	struct umoxa_g2_txq q;
	uint8_t bad[16] = { 0, 0, 0, 2, 0, 0, 0, 0, 0, 4, 0, 2 };
	uint32_t mask;

	CHECK(setup(&q) == 0);
	CHECK(send_done(&q, 0, 1, 600, 1, 0) == 0);
	CHECK(send_done(&q, 0, 2, 1, 2, 1) == 0);
	CHECK(umoxa_g2_txq_events(&q, bad, sizeof(bad), &mask) == EPROTO);
	CHECK(mask == 15 && q.ports[0].error == EPROTO);
	CHECK(umoxa_g2_txq_submit(&q, 0, 1, 3, payload, 1) == EPROTO);
	CHECK(setup(&q) == 0);
	CHECK(umoxa_g2_txq_events(&q, bad, 9, &mask) == EPROTO && mask == 15);
	CHECK(setup(&q) == 0);
	CHECK(umoxa_g2_txq_events(&q, NULL, 8, &mask) == EINVAL && mask == 0);
	CHECK(umoxa_g2_txq_events(&q, NULL, 0, &mask) == 0 && mask == 0);
	CHECK(umoxa_g2_txq_fault_all(&q, -1) == 15);
	CHECK(q.ports[0].error == EIO);
	CHECK(umoxa_g2_txq_fault_all(&q, EIO) == 0);
	return 0;
}

static int
clock_token_overflow(void)
{
	struct umoxa_g2_txq q, saved;
	struct umoxa_g2_xfer x;
	uint32_t mask;

	CHECK(setup(&q) == 0);
	CHECK(umoxa_g2_txq_tick(&q, 10, &mask) == 0);
	saved = q;
	CHECK(umoxa_g2_txq_tick(&q, 9, &mask) == EINVAL && mask == 0);
	CHECK(umoxa_g2_txq_pick(&q, 9, &x) == EINVAL);
	CHECK(memcmp(&q, &saved, sizeof(q)) == 0);
	CHECK(umoxa_g2_txq_submit(&q, 0, 1, 1, payload, 1) == 0);
	CHECK(umoxa_g2_txq_pick(&q, UINT64_MAX - 99, &x) == EOVERFLOW);
	CHECK(x.fault_mask == 15 && !q.active);
	CHECK(setup(&q) == 0);
	/* Inject the otherwise unreachable lifetime exhaustion boundary. */
	q.last_token = UINT64_MAX;
	CHECK(umoxa_g2_txq_submit(&q, 0, 1, 1, payload, 1) == 0);
	CHECK(umoxa_g2_txq_pick(&q, 1, &x) == EOVERFLOW);
	CHECK(x.fault_mask == 15 && q.last_token == UINT64_MAX);
	return 0;
}

static int
explicit_local_fault(void)
{
	struct umoxa_g2_txq q;
	struct umoxa_g2_xfer x;
	struct umoxa_g2_result r;
	uint8_t saved[36];

	CHECK(setup(&q) == 0);
	CHECK(umoxa_g2_txq_submit(&q, 0, 1, 1, payload, 32) == 0);
	CHECK(umoxa_g2_txq_submit(&q, 1, 1, 1, payload, 8) == 0);
	CHECK(umoxa_g2_txq_pick(&q, 0, &x) == 0 && x.port == 0);
	memcpy(saved, x.data, sizeof(saved));
	CHECK(umoxa_g2_txq_fault_port(&q, 0, ENOBUFS) == 1);
	CHECK(q.ports[0].state == UMOXA_FAULT && q.ports[0].error == ENOBUFS);
	CHECK(q.ports[1].state == UMOXA_OPEN && q.ports[1].job == UMOXA_QUEUED);
	CHECK(q.active && !memcmp(x.data, saved, sizeof(saved)));
	CHECK(umoxa_g2_txq_fault_port(&q, 0, EIO) == 0);
	CHECK(umoxa_g2_txq_fault_port(&q, 4, EIO) == 0);
	CHECK(umoxa_g2_txq_fault_port(NULL, 0, EIO) == 0);
	CHECK(umoxa_g2_txq_done(&q, x.token, x.len, 0, &r) == 1 && !r.deliver);
	CHECK(umoxa_g2_txq_pick(&q, 1, &x) == 0 && x.port == 1);
	CHECK(umoxa_g2_txq_done(&q, x.token, x.len, 0, &r) == 1 && r.deliver && r.actual == 8);
	return 0;
}

int
main(void)
{
	static const struct {
		const char *name;
		int (*run)(void);
	} tests[] = {
		{ "init_invalid", init_invalid },
		{ "copied_bounded_queue", copied_bounded_queue },
		{ "round_robin", round_robin },
		{ "send_next_order", send_next_order },
		{ "early_duplicate_events", early_duplicate_events },
		{ "local_timeout", local_timeout },
		{ "shared_failure_watchdog", shared_failure_watchdog },
		{ "close_reopen_barrier", close_reopen_barrier },
		{ "stale_done", stale_done },
		{ "malformed_events", malformed_events },
		{ "clock_token_overflow", clock_token_overflow },
		{ "explicit_local_fault", explicit_local_fault },
	};
	size_t i;

	memset(payload, 0x5a, sizeof(payload));
	for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
		if (tests[i].run() != 0)
			return 1;
		printf("PASS %s\n", tests[i].name);
	}
	puts("12 shared G2 TX groups, 0 failures");
	return 0;
}
