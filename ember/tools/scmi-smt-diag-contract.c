/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: actual-source SCMI discovery wire contracts, AI-assisted. */
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "scmi_smt_diag.h"

#ifdef SCMI_SMT_MISSING_SOURCE
int
scmi_smt_diag_init(struct scmi_smt_diag *s, const struct scmi_smt_diag_io *io)
{
	(void)s; (void)io;
	return ENOTSUP;
}
int
scmi_smt_diag_query(struct scmi_smt_diag *s, enum scmi_smt_diag_cmd cmd,
    uint32_t arg, struct scmi_smt_diag_reply *r, struct scmi_smt_diag_trace *t)
{
	(void)s; (void)cmd; (void)arg; (void)r; (void)t;
	return ENOTSUP;
}
#endif

static unsigned int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    if (failures < 20) fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #c); \
} } while (0)

struct fixture {
	enum scmi_smt_diag_cmd cmd;
	uint32_t argument, header, request_length, data_length;
};
/* Literal protocol/message fixtures, independent of the implementation. */
static const struct fixture fixtures[] = {
	{ SCMI_SMT_DIAG_BASE_VERSION, 0, 0x4000, 4, 4 },
	{ SCMI_SMT_DIAG_BASE_ATTRIBUTES, 0, 0x4001, 4, 4 },
	{ SCMI_SMT_DIAG_BASE_LIST_PROTOCOLS, 255, 0x4006, 8, 8 },
	{ SCMI_SMT_DIAG_CLOCK_ATTRIBUTES, 0, 0x5001, 4, 4 },
	{ SCMI_SMT_DIAG_CLOCK_ENTITY_ATTRIBUTES, 67, 0x5003, 8, 20 },
	{ SCMI_SMT_DIAG_CLOCK_RATE_GET, 67, 0x5006, 8, 8 }
};

struct device {
	uint32_t words[33], response[32];
	const struct fixture *fixture;
	uint32_t token, initial_status, status, header_xor, length;
	uint32_t response_status, list_count;
	unsigned int calls, reads, writes, pauses, clock_calls, rings;
	unsigned int published, busy, sent, received, bad_request;
	unsigned int no_response, frozen, rollback, jump, zero_rate;
	uint64_t now;
};

static void
model_reset(struct device *d, const struct fixture *f, unsigned int token)
{
	memset(d, 0, sizeof(*d));
	d->fixture = f;
	d->token = token;
	d->initial_status = 1;
	d->response_status = 1;
	d->list_count = 4;
	d->length = 8 + f->data_length;
	d->now = 1000000 + (uint64_t)token * 1000;
	d->words[2] = 0x1234abcd; /* Implementation-defined words must survive. */
	d->words[3] = 0x5678ef90;
}

static uint32_t
model_read(void *cookie, uint32_t off)
{
	struct device *d = cookie;
	d->calls++;
	d->reads++;
	CHECK(off <= 0x7c && (off & 3) == 0);
	if (off > 0x7c || (off & 3) != 0)
		return 0;
	if (off == 4 && !d->busy)
		return d->initial_status;
	return d->words[off / 4];
}

static int
model_request_valid(const struct device *d)
{
	const struct fixture *f = d->fixture;
	return d->words[0] == 0 && d->words[4] == 0 &&
	    d->words[5] == f->request_length &&
	    d->words[6] == (f->header | (d->token << 18)) &&
	    (f->request_length == 4 || d->words[7] == f->argument) &&
	    d->words[2] == 0x1234abcd && d->words[3] == 0x5678ef90;
}

static void
model_write(void *cookie, uint32_t off, uint32_t value)
{
	struct device *d = cookie;
	d->calls++;
	d->writes++;
	CHECK(off == 0 || off == 4 || off == 16 || off == 20 ||
	    off == 24 || off == 28 || off == 0x80);
	if (off > 0x80 || (off & 3) != 0)
		return;
	if (off == 0x80) {
		d->rings++;
		/* The server cannot answer an unpublished/reordered request. */
		if (!d->published || !d->busy || !d->sent || value != 1 ||
		    !model_request_valid(d))
			d->bad_request = 1;
		return;
	}
	if (off == 4) {
		/* The platform may observe busy before the doorbell. */
		if (!d->published || !model_request_valid(d) || value != 0)
			d->bad_request = 1;
		d->busy = 1;
	} else {
		d->published = 0;
	}
	d->words[off / 4] = value;
}

static void
model_barrier(void *cookie, enum scmi_smt_diag_barrier kind)
{
	struct device *d = cookie;
	d->calls++;
	switch (kind) {
	case SCMI_SMT_DIAG_PUBLISH:
		d->published = model_request_valid(d);
		break;
	case SCMI_SMT_DIAG_SEND:
		d->sent = d->published && d->busy;
		break;
	case SCMI_SMT_DIAG_RECEIVE:
		/* Response words become visible only after the acquire barrier. */
		if (d->words[1] != 0) {
			memcpy(d->words, d->response, sizeof(d->response));
			d->received = 1;
		}
		break;
	default:
		CHECK(0);
	}
}

static uint64_t
model_now(void *cookie)
{
	struct device *d = cookie;
	d->calls++;
	d->clock_calls++;
	if (d->rollback != 0 && d->clock_calls == d->rollback)
		return d->now - 10000;
	return d->now;
}

static void
model_pause(void *cookie)
{
	struct device *d = cookie;
	uint32_t i;
	d->calls++;
	d->pauses++;
	if (!d->frozen)
		d->now += d->jump ? 30000 : 10;
	if (!d->rings || d->bad_request || d->no_response || d->pauses < 2)
		return;
	memset(d->response, 0, sizeof(d->response));
	d->response[1] = d->response_status;
	d->response[2] = 0x1234abcd;
	d->response[3] = 0x5678ef90;
	d->response[5] = d->length;
	d->response[6] = (d->fixture->header | (d->token << 18)) ^ d->header_xor;
	d->response[7] = d->status;
	for (i = 8; i < 32; i++)
		d->response[i] = 0x12345678;
	if (d->fixture->cmd == SCMI_SMT_DIAG_BASE_LIST_PROTOCOLS) {
		d->response[8] = d->list_count;
		d->response[9] = 0x15141311;
	}
	if (d->fixture->cmd == SCMI_SMT_DIAG_CLOCK_RATE_GET) {
		d->response[9] = 0x9abcdef0;
		if (d->zero_rate)
			d->response[8] = d->response[9] = 0;
	}
	d->words[1] = d->response_status;
}

static struct scmi_smt_diag_io
model_io(struct device *d)
{
	struct scmi_smt_diag_io io = {
		.cookie = d, .read_4 = model_read, .write_4 = model_write,
		.barrier = model_barrier, .now_us = model_now, .pause = model_pause
	};
	return io;
}

static void
query_failure(struct scmi_smt_diag *s, struct device *d, int expected,
    enum scmi_smt_diag_stage stage, int poison)
{
	struct scmi_smt_diag_reply r = { 0 }, before;
	struct scmi_smt_diag_trace t = { 0 }, tb;
	struct scmi_smt_diag_io io = model_io(d);
	unsigned int calls;
	memset(&r, 0xa5, sizeof(r));
	before = r;
	memset(&t, 0x5a, sizeof(t));
	CHECK(scmi_smt_diag_query(s, d->fixture->cmd, d->fixture->argument,
	    &r, &t) == expected);
	CHECK(memcmp(&r, &before, sizeof(r)) == 0);
	CHECK(t.stage == stage);
	CHECK(d->pauses <= 3000);
	if (!poison)
		return;
	calls = d->calls;
	tb = t;
	CHECK(scmi_smt_diag_init(s, &io) == EBUSY);
	CHECK(scmi_smt_diag_query(s, d->fixture->cmd, d->fixture->argument,
	    &r, &t) == EIO);
	CHECK(d->calls == calls);
	CHECK(memcmp(&t, &tb, sizeof(t)) == 0);
	CHECK(memcmp(&r, &before, sizeof(r)) == 0);
}

static void
test_success(void)
{
	struct scmi_smt_diag s = SCMI_SMT_DIAG_INITIALIZER;
	struct scmi_smt_diag_reply r = { 0 };
	struct scmi_smt_diag_trace t = { 0 };
	struct device d;
	struct scmi_smt_diag_io io = model_io(&d);
	size_t i;
	model_reset(&d, &fixtures[0], 1);
	CHECK(scmi_smt_diag_init(&s, &io) == 0);
	CHECK(d.calls == 0);
	CHECK(scmi_smt_diag_init(&s, &io) == EBUSY);
	CHECK(d.calls == 0);
	for (i = 0; i < sizeof(fixtures) / sizeof(fixtures[0]); i++) {
		model_reset(&d, &fixtures[i], (unsigned int)i + 1);
		memset(&r, 0xa5, sizeof(r));
		CHECK(scmi_smt_diag_query(&s, fixtures[i].cmd, fixtures[i].argument,
		    &r, &t) == 0);
		CHECK(r.data_length == fixtures[i].data_length);
		CHECK(t.stage == SCMI_SMT_DIAG_COMPLETE && t.firmware_status_valid);
		CHECK(t.firmware_status == 0 && d.rings == 1 && d.received);
		CHECK(!d.bad_request && d.pauses == 2);
		CHECK(t.request_header == (fixtures[i].header | ((i + 1) << 18)));
		CHECK(t.response_header == t.request_header);
		CHECK(t.response_length == 8 + fixtures[i].data_length);
		CHECK(r.data[0] == (i == 2 ? 4 : 0x78));
		if (i == 5) {
			static const uint8_t rate[] = { 0x78, 0x56, 0x34, 0x12,
			    0xf0, 0xde, 0xbc, 0x9a };
			CHECK(memcmp(r.data, rate, sizeof(rate)) == 0);
		}
	}
}

static void
test_api(void)
{
	struct scmi_smt_diag s = SCMI_SMT_DIAG_INITIALIZER, before;
	struct scmi_smt_diag_reply r = { 0 }, rb;
	struct scmi_smt_diag_trace t = { 0 }, tb;
	struct device d;
	struct scmi_smt_diag_io io = model_io(&d), bad;
	uint32_t ids[] = { 0, 66, 68, 264, 266, 271, UINT32_MAX };
	size_t i;
	model_reset(&d, &fixtures[0], 1);
	memset(&r, 0xa5, sizeof(r)); rb = r;
	memset(&t, 0x5a, sizeof(t)); tb = t;
	before = s;
	CHECK(scmi_smt_diag_init(NULL, &io) == EINVAL);
	CHECK(scmi_smt_diag_init(&s, NULL) == EINVAL);
	for (i = 0; i < 5; i++) {
		bad = io;
		switch (i) {
		case 0: bad.read_4 = NULL; break;
		case 1: bad.write_4 = NULL; break;
		case 2: bad.barrier = NULL; break;
		case 3: bad.now_us = NULL; break;
		case 4: bad.pause = NULL; break;
		}
		CHECK(scmi_smt_diag_init(&s, &bad) == EINVAL);
		CHECK(memcmp(&s, &before, sizeof(s)) == 0);
	}
	CHECK(scmi_smt_diag_query(&s, fixtures[0].cmd, 0, &r, &t) == EINVAL);
	CHECK(scmi_smt_diag_init(&s, &io) == 0);
	CHECK(scmi_smt_diag_query(NULL, fixtures[0].cmd, 0, &r, &t) == EINVAL);
	CHECK(scmi_smt_diag_query(&s, fixtures[0].cmd, 0, NULL, &t) == EINVAL);
	CHECK(scmi_smt_diag_query(&s, fixtures[0].cmd, 0, &r, NULL) == EINVAL);
	CHECK(scmi_smt_diag_query(&s, (enum scmi_smt_diag_cmd)-1, 0, &r, &t) == EINVAL);
	CHECK(scmi_smt_diag_query(&s, (enum scmi_smt_diag_cmd)99, 0, &r, &t) == EINVAL);
	CHECK(scmi_smt_diag_query(&s, SCMI_SMT_DIAG_BASE_VERSION, 1, &r, &t) == EINVAL);
	CHECK(scmi_smt_diag_query(&s, SCMI_SMT_DIAG_BASE_LIST_PROTOCOLS, 256,
	    &r, &t) == EINVAL);
	for (i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
		CHECK(scmi_smt_diag_query(&s, SCMI_SMT_DIAG_CLOCK_ENTITY_ATTRIBUTES,
		    ids[i], &r, &t) == EINVAL);
		CHECK(scmi_smt_diag_query(&s, SCMI_SMT_DIAG_CLOCK_RATE_GET,
		    ids[i], &r, &t) == EINVAL);
	}
	CHECK(d.calls == 0 && memcmp(&r, &rb, sizeof(r)) == 0);
	CHECK(memcmp(&t, &tb, sizeof(t)) == 0);
}

static void
test_framing(void)
{
	const uint32_t lengths[] = { 0, 1, 4, 7, 9, 11, 105, 108, UINT32_MAX };
	const uint32_t headers[] = { 1, 0x100, 0x200, 0x300, 0x400, 0x40000,
	    0x10000000, 0x80000000 };
	const uint32_t channel[] = { 2, 3, 4, 5, 6, 7, 0x80000000, 0x80000001 };
	size_t i, j;
	for (i = 0; i < sizeof(fixtures) / sizeof(fixtures[0]); i++) {
		for (j = 0; j < sizeof(lengths) / sizeof(lengths[0]) + 1; j++) {
			struct scmi_smt_diag s = SCMI_SMT_DIAG_INITIALIZER;
			struct device d;
			struct scmi_smt_diag_io io = model_io(&d);
			model_reset(&d, &fixtures[i], 1);
			d.length = j < sizeof(lengths) / sizeof(lengths[0]) ?
			    lengths[j] : 4 + fixtures[i].data_length;
			CHECK(scmi_smt_diag_init(&s, &io) == 0);
			query_failure(&s, &d, EPROTO, SCMI_SMT_DIAG_LENGTH, 1);
		}
	}
	for (i = 0; i < sizeof(headers) / sizeof(headers[0]); i++) {
		struct scmi_smt_diag s = SCMI_SMT_DIAG_INITIALIZER;
		struct device d;
		struct scmi_smt_diag_io io = model_io(&d);
		model_reset(&d, &fixtures[5], 1);
		d.header_xor = headers[i];
		CHECK(scmi_smt_diag_init(&s, &io) == 0);
		query_failure(&s, &d, EPROTO, SCMI_SMT_DIAG_HEADER, 1);
	}
	for (i = 0; i < sizeof(channel) / sizeof(channel[0]); i++) {
		struct scmi_smt_diag s = SCMI_SMT_DIAG_INITIALIZER;
		struct device d;
		struct scmi_smt_diag_io io = model_io(&d);
		model_reset(&d, &fixtures[5], 1);
		d.initial_status = channel[i];
		CHECK(scmi_smt_diag_init(&s, &io) == 0);
		query_failure(&s, &d, EPROTO, SCMI_SMT_DIAG_CHANNEL, 1);
		CHECK(d.writes == 0 && d.rings == 0);
		s = (struct scmi_smt_diag)SCMI_SMT_DIAG_INITIALIZER;
		model_reset(&d, &fixtures[5], 1);
		d.response_status = channel[i];
		CHECK(scmi_smt_diag_init(&s, &io) == 0);
		query_failure(&s, &d, EPROTO, SCMI_SMT_DIAG_CHANNEL, 1);
	}
}

static void
test_status_and_list(void)
{
	const uint32_t statuses[] = { 1, 6, 11, 0x100, 0xfffffffc,
	    0xffffffff, 0x80000000, 0x7fffffff };
	const uint32_t counts[] = { 0, 1, 4, 5, 88, 89, UINT32_MAX };
	size_t i;
	for (i = 0; i < sizeof(statuses) / sizeof(statuses[0]); i++) {
		struct scmi_smt_diag s = SCMI_SMT_DIAG_INITIALIZER;
		struct scmi_smt_diag_reply r = { 0 }, before;
		struct scmi_smt_diag_trace t = { 0 };
		struct device d;
		struct scmi_smt_diag_io io = model_io(&d);
		model_reset(&d, &fixtures[5], 1);
		d.status = statuses[i]; d.length = 8;
		memset(&r, 0xa5, sizeof(r)); before = r;
		CHECK(scmi_smt_diag_init(&s, &io) == 0);
		CHECK(scmi_smt_diag_query(&s, fixtures[5].cmd, 67, &r, &t) == EIO);
		CHECK(t.firmware_status_valid && (uint32_t)t.firmware_status == statuses[i]);
		CHECK(t.stage == SCMI_SMT_DIAG_FIRMWARE);
		CHECK(memcmp(&r, &before, sizeof(r)) == 0);
		model_reset(&d, &fixtures[5], 2);
		CHECK(scmi_smt_diag_query(&s, fixtures[5].cmd, 67, &r, &t) == 0);
	}
	for (i = 0; i < sizeof(counts) / sizeof(counts[0]); i++) {
		struct scmi_smt_diag s = SCMI_SMT_DIAG_INITIALIZER;
		struct scmi_smt_diag_reply r = { 0 }, before;
		struct scmi_smt_diag_trace t = { 0 };
		struct device d;
		struct scmi_smt_diag_io io = model_io(&d);
		model_reset(&d, &fixtures[2], 1);
		d.list_count = counts[i];
		/* Body has count plus exactly4 protocol bytes. */
		memset(&r, 0xa5, sizeof(r)); before = r;
		CHECK(scmi_smt_diag_init(&s, &io) == 0);
		CHECK(scmi_smt_diag_query(&s, fixtures[2].cmd, 255, &r, &t) ==
		    (counts[i] <= 4 ? 0 : EPROTO));
		if (counts[i] > 4)
			CHECK(memcmp(&r, &before, sizeof(r)) == 0);
	}
	{
		struct scmi_smt_diag s = SCMI_SMT_DIAG_INITIALIZER;
		struct scmi_smt_diag_reply r = { 0 };
		struct scmi_smt_diag_trace t = { 0 };
		struct device d;
		struct scmi_smt_diag_io io = model_io(&d);
		model_reset(&d, &fixtures[2], 1);
		d.list_count = 92; d.length = 104;
		CHECK(scmi_smt_diag_init(&s, &io) == 0);
		CHECK(scmi_smt_diag_query(&s, fixtures[2].cmd, 255, &r, &t) == 0);
		CHECK(r.data_length == 96);
	}
}

static void
test_waits(void)
{
	unsigned int mode, phase;
	for (phase = 0; phase < 2; phase++) {
		for (mode = 0; mode < 5; mode++) {
			struct scmi_smt_diag s = SCMI_SMT_DIAG_INITIALIZER;
			struct device d;
			struct scmi_smt_diag_io io = model_io(&d);
			model_reset(&d, &fixtures[5], 1);
			d.initial_status = phase ? 1 : 0;
			d.no_response = 1;
			if (mode == 1) d.frozen = 1;
			if (mode == 2) d.rollback = 2;
			if (mode == 3) d.now = UINT64_MAX - 10;
			if (mode == 4) d.jump = 1;
			CHECK(scmi_smt_diag_init(&s, &io) == 0);
			query_failure(&s, &d, mode == 2 || mode == 3 ? EIO :
			    (phase ? ETIMEDOUT : EBUSY),
			    mode == 2 || mode == 3 ? SCMI_SMT_DIAG_CLOCK :
			    (phase ? SCMI_SMT_DIAG_WAIT_RESPONSE : SCMI_SMT_DIAG_WAIT_FREE),
			    phase || mode == 2 || mode == 3);
			CHECK(phase ? d.rings == 1 : d.writes == 0);
			CHECK(d.reads <= 3005 && d.clock_calls <= 3002);
		}
	}
}

static void
test_boundaries(void)
{
	struct scmi_smt_diag s = SCMI_SMT_DIAG_INITIALIZER;
	struct scmi_smt_diag_reply r = { 0 };
	struct scmi_smt_diag_trace t = { 0 };
	struct device d;
	struct scmi_smt_diag_io io = model_io(&d);
	unsigned int i;
	uint64_t after;
	model_reset(&d, &fixtures[5], 1);
	d.initial_status = 0; d.frozen = 1;
	CHECK(scmi_smt_diag_init(&s, &io) == 0);
	query_failure(&s, &d, EBUSY, SCMI_SMT_DIAG_WAIT_FREE, 0);
	CHECK(d.writes == 0);
	after = d.now + 100;
	model_reset(&d, &fixtures[5], 1);
	d.now = after;
	d.zero_rate = 1;
	CHECK(scmi_smt_diag_query(&s, fixtures[5].cmd, 67, &r, &t) == 0);
	CHECK(t.request_header == 0x45006 && r.data_length == 8);
	for (i = 0; i < 8; i++)
		CHECK(r.data[i] == 0);
	model_reset(&d, &fixtures[2], 2);
	d.length = 12; d.list_count = 0;
	CHECK(scmi_smt_diag_query(&s, fixtures[2].cmd, 255, &r, &t) == 0);
	CHECK(r.data_length == 4);
	model_reset(&d, &fixtures[2], 3);
	d.length = 12; d.list_count = 1;
	query_failure(&s, &d, EPROTO, SCMI_SMT_DIAG_LENGTH, 1);
}

static void
test_token_lifetime(void)
{
	struct scmi_smt_diag s = SCMI_SMT_DIAG_INITIALIZER;
	struct scmi_smt_diag_reply r = { 0 }, before;
	struct scmi_smt_diag_trace t = { 0 }, tb;
	struct device d;
	struct scmi_smt_diag_io io = model_io(&d);
	unsigned int i, calls;
	model_reset(&d, &fixtures[0], 1);
	CHECK(scmi_smt_diag_init(&s, &io) == 0);
	for (i = 1; i <= 1023; i++) {
		model_reset(&d, &fixtures[0], i);
		CHECK(scmi_smt_diag_query(&s, fixtures[0].cmd, 0, &r, &t) == 0);
	}
	before = r; tb = t; calls = d.calls;
	CHECK(scmi_smt_diag_query(&s, fixtures[0].cmd, 0, &r, &t) == EOVERFLOW);
	CHECK(scmi_smt_diag_init(&s, &io) == EBUSY);
	CHECK(d.calls == calls && memcmp(&r, &before, sizeof(r)) == 0);
	CHECK(memcmp(&t, &tb, sizeof(t)) == 0);
}

int
main(void)
{
	struct scmi_smt_diag s = SCMI_SMT_DIAG_INITIALIZER;
	struct device d;
	struct scmi_smt_diag_io io = model_io(&d);
	alarm(30);
	model_reset(&d, &fixtures[0], 1);
	CHECK(scmi_smt_diag_init(&s, &io) == 0);
	if (failures != 0) {
		printf("SCMI SMT contract: %u checks, %u failures (missing wire core)\n",
		    checks, failures);
		return 1;
	}
	test_success();
	test_api();
	test_framing();
	test_status_and_list();
	test_waits();
	test_boundaries();
	test_token_lifetime();
	alarm(0);
	printf("SCMI SMT contract: %u checks, %u failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
