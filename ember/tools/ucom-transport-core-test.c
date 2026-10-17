/* Origin: EmberBSD ucom transport state contract tests, 2026-10-09. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "ucom_transport_core.h"

#define CHECK(x) do { \
	if (!(x)) { \
		fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #x); \
		return 0; \
	} \
} while (0)

/* Padding is not state and need not be initialized by a field-wise init. */
static int
same_state(const struct ucom_transport_core *a,
    const struct ucom_transport_core *b)
{

	return a->state == b->state && a->epoch == b->epoch &&
	    a->cookie == b->cookie && a->tx_length == b->tx_length &&
	    a->tx_pending == b->tx_pending && a->error == b->error;
}

static int
test_open_close_epoch(void)
{
	struct ucom_transport_core s;
	uint64_t epoch = 99, cookie = 99;

	ucom_transport_core_init(&s);
	CHECK(s.state == UCT_CLOSED && s.epoch == 0 && s.cookie == 0);
	CHECK(s.tx_length == 0 && s.tx_pending == 0 && s.error == 0);
	CHECK(ucom_transport_core_open(&s, &epoch) == 0 && epoch == 1);
	CHECK(ucom_transport_core_accept(&s, epoch));
	CHECK(ucom_transport_core_open(&s, &epoch) == EBUSY && epoch == 0);
	CHECK(ucom_transport_core_begin(&s, 8, &cookie) == 0 && cookie == 1);
	ucom_transport_core_close(&s);
	CHECK(s.state == UCT_CLOSED && s.tx_length == 0 && !s.tx_pending);
	CHECK(!ucom_transport_core_accept(&s, 1));
	ucom_transport_core_close(&s);
	CHECK(ucom_transport_core_open(&s, &epoch) == 0 && epoch == 2);
	CHECK(s.cookie == 0 && s.error == 0);
	return 1;
}

static int
test_duplicate_done(void)
{
	struct ucom_transport_core s, before;
	uint64_t epoch, cookie, second;
	size_t consumed = 99;

	ucom_transport_core_init(&s);
	CHECK(ucom_transport_core_open(&s, &epoch) == 0);
	CHECK(ucom_transport_core_begin(&s, 8, &cookie) == 0);
	CHECK(ucom_transport_core_done(&s, epoch, cookie, 8, 0, &consumed) == 1);
	CHECK(consumed == 8 && !s.tx_pending && s.tx_length == 0);
	CHECK(ucom_transport_core_begin(&s, 16, &second) == 0 && second == cookie + 1);
	memcpy(&before, &s, sizeof(s));
	CHECK(ucom_transport_core_done(&s, epoch, cookie, 8, 0, &consumed) == 0);
	CHECK(consumed == 0 && same_state(&s, &before));
	CHECK(ucom_transport_core_done(&s, epoch, second, 16, 0, &consumed) == 1);
	CHECK(consumed == 16);
	CHECK(ucom_transport_core_done(&s, epoch, second, 16, 0, &consumed) == 0);
	CHECK(consumed == 0);
	return 1;
}

static int
test_old_epoch(void)
{
	struct ucom_transport_core s, before;
	uint64_t epoch, old_epoch, cookie, old_cookie;
	size_t consumed = 99;

	ucom_transport_core_init(&s);
	CHECK(ucom_transport_core_open(&s, &old_epoch) == 0);
	CHECK(ucom_transport_core_begin(&s, 8, &old_cookie) == 0);
	ucom_transport_core_close(&s);
	CHECK(ucom_transport_core_open(&s, &epoch) == 0);
	CHECK(ucom_transport_core_begin(&s, 8, &cookie) == 0);
	CHECK(cookie == old_cookie && epoch != old_epoch);
	memcpy(&before, &s, sizeof(s));
	CHECK(!ucom_transport_core_accept(&s, old_epoch));
	CHECK(ucom_transport_core_done(&s, old_epoch, old_cookie, 8, 0,
	    &consumed) == 0);
	CHECK(consumed == 0 && same_state(&s, &before));
	CHECK(ucom_transport_core_done(&s, old_epoch, old_cookie, 0, EIO,
	    &consumed) == 0 && s.state == UCT_OPEN);
	return 1;
}

static int
test_partial_fault(void)
{
	struct ucom_transport_core s;
	uint64_t epoch, cookie;
	size_t consumed;
	const size_t actual[] = {0, 7, 9, 8, 7, 8};
	const int errors[] = {0, 0, 0, ETIMEDOUT, EPIPE, -1};
	unsigned int i;
	int expected;

	for (i = 0; i < sizeof(actual) / sizeof(actual[0]); i++) {
		ucom_transport_core_init(&s);
		CHECK(ucom_transport_core_open(&s, &epoch) == 0);
		CHECK(ucom_transport_core_begin(&s, 8, &cookie) == 0);
		consumed = 99;
		CHECK(ucom_transport_core_done(&s, epoch, cookie, actual[i],
		    errors[i], &consumed) == 1);
		expected = errors[i] > 0 ? errors[i] : EIO;
		CHECK(consumed == 0 && s.state == UCT_FAULT && s.error == expected);
		CHECK(!s.tx_pending && s.tx_length == 0);
		CHECK(!ucom_transport_core_accept(&s, epoch));
		CHECK(ucom_transport_core_open(&s, &epoch) == EIO && epoch == 0);
		CHECK(ucom_transport_core_begin(&s, 8, &cookie) == EIO && cookie == 0);
		ucom_transport_core_close(&s);
		CHECK(ucom_transport_core_open(&s, &epoch) == 0 && s.error == 0);
	}
	return 1;
}

static int
test_four_port_isolation(void)
{
	struct ucom_transport_core s[4], before[4];
	uint64_t epoch[4], cookie[4];
	size_t consumed;
	unsigned int i;

	for (i = 0; i < 4; i++) {
		ucom_transport_core_init(&s[i]);
		CHECK(ucom_transport_core_open(&s[i], &epoch[i]) == 0);
		CHECK(ucom_transport_core_begin(&s[i], i + 1, &cookie[i]) == 0);
	}
	memcpy(before, s, sizeof(s));
	ucom_transport_core_fault(&s[1], EPIPE);
	CHECK(s[1].state == UCT_FAULT && s[1].error == EPIPE);
	for (i = 0; i < 4; i++) {
		if (i == 1)
			continue;
		CHECK(same_state(&s[i], &before[i]));
		CHECK(ucom_transport_core_done(&s[i], epoch[i], cookie[i],
		    i + 1, 0, &consumed) == 1 && consumed == i + 1);
	}
	return 1;
}

static int
test_detach_final(void)
{
	struct ucom_transport_core s, before;
	uint64_t epoch, cookie;
	size_t consumed = 99;

	ucom_transport_core_init(&s);
	CHECK(ucom_transport_core_open(&s, &epoch) == 0);
	CHECK(ucom_transport_core_begin(&s, 8, &cookie) == 0);
	ucom_transport_core_detach(&s);
	CHECK(s.state == UCT_DETACHED && !s.tx_pending && s.tx_length == 0);
	CHECK(s.error == ENXIO && !ucom_transport_core_accept(&s, epoch));
	memcpy(&before, &s, sizeof(s));
	ucom_transport_core_close(&s);
	ucom_transport_core_fault(&s, EIO);
	ucom_transport_core_detach(&s);
	CHECK(same_state(&s, &before));
	CHECK(ucom_transport_core_done(&s, epoch, cookie, 8, 0, &consumed) == 0);
	CHECK(consumed == 0 && same_state(&s, &before));
	CHECK(ucom_transport_core_open(&s, &epoch) == ENXIO && epoch == 0);
	CHECK(ucom_transport_core_begin(&s, 8, &cookie) == ENXIO && cookie == 0);
	return 1;
}

static int
test_counter_overflow(void)
{
	struct ucom_transport_core s;
	uint64_t epoch = 99, cookie = 99;
	size_t consumed;

	ucom_transport_core_init(&s);
	s.epoch = UINT64_MAX;
	CHECK(ucom_transport_core_open(&s, &epoch) == EOVERFLOW && epoch == 0);
	CHECK(s.state == UCT_DETACHED && s.epoch == UINT64_MAX);
	CHECK(s.error == EOVERFLOW);
	ucom_transport_core_close(&s);
	ucom_transport_core_detach(&s);
	CHECK(s.error == EOVERFLOW);
	ucom_transport_core_init(&s);
	CHECK(ucom_transport_core_open(&s, &epoch) == 0);
	s.cookie = UINT64_MAX - 1;
	CHECK(ucom_transport_core_begin(&s, 8, &cookie) == 0 && cookie == UINT64_MAX);
	CHECK(ucom_transport_core_done(&s, epoch, cookie, 8, 0, &consumed) == 1);
	CHECK(ucom_transport_core_begin(&s, 8, &cookie) == EOVERFLOW && cookie == 0);
	CHECK(s.state == UCT_FAULT && s.cookie == UINT64_MAX && s.error == EOVERFLOW);
	return 1;
}

static int
test_invalid_states(void)
{
	struct ucom_transport_core s, before;
	uint64_t epoch = 99, cookie = 99;
	size_t consumed = 99;

	ucom_transport_core_init(&s);
	CHECK(ucom_transport_core_begin(&s, 8, &cookie) == EIO && cookie == 0);
	ucom_transport_core_fault(&s, EPIPE);
	CHECK(s.state == UCT_CLOSED && s.error == 0);
	CHECK(ucom_transport_core_open(NULL, &epoch) == EINVAL && epoch == 0);
	CHECK(ucom_transport_core_open(&s, NULL) == EINVAL && s.epoch == 0);
	CHECK(!ucom_transport_core_accept(NULL, 0));
	CHECK(ucom_transport_core_done(NULL, 0, 0, 8, 0, &consumed) == 0);
	CHECK(consumed == 0);
	CHECK(ucom_transport_core_open(&s, &epoch) == 0);
	CHECK(ucom_transport_core_begin(&s, 0, &cookie) == EINVAL && cookie == 0);
	CHECK(ucom_transport_core_begin(NULL, 8, &cookie) == EINVAL && cookie == 0);
	CHECK(ucom_transport_core_begin(&s, 8, NULL) == EINVAL && !s.tx_pending);
	CHECK(ucom_transport_core_begin(&s, SIZE_MAX, &cookie) == 0);
	memcpy(&before, &s, sizeof(s));
	CHECK(ucom_transport_core_done(&s, epoch, cookie, 8, 0, NULL) == 0);
	CHECK(same_state(&s, &before));
	CHECK(ucom_transport_core_begin(&s, 8, &cookie) == EBUSY && cookie == 0);
	CHECK(ucom_transport_core_done(&s, epoch, s.cookie + 1, 8, 0, &consumed) == 0);
	CHECK(consumed == 0 && same_state(&s, &before));
	ucom_transport_core_fault(&s, 0);
	CHECK(s.state == UCT_FAULT && s.error == EIO && !s.tx_pending);
	ucom_transport_core_fault(&s, -1);
	CHECK(s.error == EIO);
	ucom_transport_core_close(&s);
	CHECK(s.error == 0 && s.tx_length == 0);
	return 1;
}

int
main(void)
{
	const struct { const char *name; int (*run)(void); } tests[] = {
		{"open_close_epoch", test_open_close_epoch},
		{"duplicate_done", test_duplicate_done},
		{"old_epoch", test_old_epoch},
		{"partial_fault", test_partial_fault},
		{"four_port_isolation", test_four_port_isolation},
		{"detach_final", test_detach_final},
		{"counter_overflow", test_counter_overflow},
		{"invalid_states", test_invalid_states}
	};
	unsigned int i, passed = 0;
	int ok;

	for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
		ok = tests[i].run();
		printf("%s %s\n", ok ? "PASS" : "FAIL", tests[i].name);
		passed += ok;
	}
	printf("ucom state: %u/8 groups passed, %u failures\n", passed, 8 - passed);
	return passed == 8 ? 0 : 1;
}
