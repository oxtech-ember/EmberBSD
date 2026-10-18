/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual new clock core and unchanged BSD HSM reference. Firmware is fake. */
#if defined(__linux__) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE 1
#endif
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define htole32 OSSwapHostToLittleInt32
#define le32toh OSSwapLittleToHostInt32
#elif defined(__linux__)
#include <endian.h>
#else
#include <sys/endian.h>
#endif
#ifndef __aligned
#define __aligned(n) __attribute__((__aligned__(n)))
#endif
#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif
#include "display.h"
#include "clockvar.h"
#include "hsm-types.h"
#include "properties.h"
#include "clock-core.h"
#include "hsm-reference.h"

static unsigned int checks;
#define CHECK(e) do { checks++; if (!(e)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #e); exit(1); } } while (0)

#define LOG_MAX 128
struct request {
	uint32_t word[9];
	size_t length;
};
struct firmware {
	uint32_t id, state, rate, minimum, maximum, round;
	unsigned int calls, sets, enables, disables;
	unsigned int fail_call, bad_call;
	unsigned int bad_word;
	uint32_t bad_value;
	bool fail_after_set;
	struct request request[LOG_MAX];
};

static struct firmware
firmware_new(enum bcm2712_clock_profile profile, uint32_t state, uint32_t rate)
{
	struct firmware f;

	memset(&f, 0, sizeof(f));
	f.id = profile == BCM2712_CLOCK_HSM ? VCPROP_CLK_M2MC :
	    VCPROP_CLK_PIXEL_BVB;
	f.state = state;
	f.rate = rate;
	f.minimum = 23000000;
	f.maximum = 300000000;
	return f;
}

static int
exchange(void *arg, uint32_t *p, size_t length)
{
	struct firmware *f = arg;
	uint32_t tag = le32toh(p[2]), value = le32toh(p[6]);
	size_t words = length / sizeof(*p);
	bool set = tag == VCPROPTAG_SET_CLOCKRATE ||
	    tag == VCPROPTAG_SET_CLOCKSTATE;

	CHECK(f->calls < LOG_MAX);
	CHECK(((uintptr_t)p & 15) == 0);
	CHECK(le32toh(p[0]) == length && p[1] == 0 && p[4] == 0);
	CHECK(le32toh(p[5]) == f->id && p[words - 1] == 0);
	if (tag == VCPROPTAG_SET_CLOCKRATE) {
		CHECK(words == 9 && le32toh(p[3]) == 12);
		CHECK(le32toh(p[7]) == 1);
	} else {
		CHECK(words == 8 && le32toh(p[3]) == 8);
	}
	f->request[f->calls].length = length;
	memcpy(f->request[f->calls].word, p, length);
	f->calls++;
	if (f->fail_call == f->calls && !(set && f->fail_after_set))
		return EIO;
	switch (tag) {
	case VCPROPTAG_GET_CLOCKSTATE:
		value = f->state;
		break;
	case VCPROPTAG_GET_CLOCKRATE:
		value = f->rate;
		break;
	case VCPROPTAG_GET_MIN_CLOCKRATE:
		value = f->minimum;
		break;
	case VCPROPTAG_GET_MAX_CLOCKRATE:
		value = f->maximum;
		break;
	case VCPROPTAG_SET_CLOCKRATE:
		f->sets++;
		f->rate = value + f->round;
		value = f->rate;
		break;
	case VCPROPTAG_SET_CLOCKSTATE:
		f->sets++;
		if (value == 1)
			f->enables++;
		else {
			CHECK(value == 0);
			f->disables++;
		}
		f->state = value;
		break;
	default:
		CHECK(false);
	}
	if (f->fail_call == f->calls)
		return EIO;
	p[1] = htole32(VCPROP_REQ_SUCCESS);
	p[4] = htole32(VCPROPTAG_RESPONSE | 8);
	p[6] = htole32(value);
	if (f->bad_call == f->calls) {
		CHECK(f->bad_word < words);
		p[f->bad_word] = htole32(f->bad_value);
	}
	return 0;
}

static void
trace_equal(const struct firmware *a, const struct firmware *b)
{
	CHECK(a->calls == b->calls && a->sets == b->sets);
	CHECK(a->enables == b->enables && a->disables == b->disables);
	CHECK(a->state == b->state && a->rate == b->rate);
	CHECK(memcmp(a->request, b->request, sizeof(a->request)) == 0);
}

static void
hsm_equal(const struct bcm2712_clock *a, const struct bcm2712_hsm *b)
{
	CHECK(a->status.floor == b->floor);
	CHECK(a->status.rate == b->status.rate);
	CHECK(a->status.state == b->status.state);
	CHECK(a->status.leases == b->status.leases);
	CHECK(a->status.fault == b->status.fault);
	CHECK(a->status.mutation_attempted == b->status.mutation_attempted);
	CHECK(memcmp(a->status.minimum, b->status.minimum,
	    sizeof(a->status.minimum)) == 0);
	CHECK(a->initialized == b->initialized);
	CHECK(a->initial_state == b->initial_state);
	CHECK(a->rate_changed == b->rate_changed);
	CHECK(a->state_changed == b->state_changed);
}

static void
hsm_trace(uint32_t state, uint32_t rate, unsigned int fail, bool malformed)
{
	struct firmware a = firmware_new(BCM2712_CLOCK_HSM, state, rate);
	struct firmware b;
	struct bcm2712_clock core;
	struct bcm2712_hsm reference;
	int actual, expected;
	unsigned int operation;

	if (malformed) {
		a.bad_call = fail;
		a.bad_word = 5;
		a.bad_value = VCPROP_CLK_PIXEL_BVB;
	} else {
		a.fail_call = fail;
		a.fail_after_set = true;
	}
	b = a;
	actual = bcm2712_clock_init(&core, BCM2712_CLOCK_HSM, exchange, &a);
	expected = bcm2712_hsm_init(&reference, exchange, &b);
	CHECK(actual == expected);
	trace_equal(&a, &b);
	if (actual != 0)
		return;
	hsm_equal(&core, &reference);
	for (operation = 0; operation < 6; operation++) {
		switch (operation) {
		case 0:
			actual = bcm2712_clock_acquire(&core, 0, 150000000);
			expected = bcm2712_hsm_acquire(&reference, 0, 150000000);
			break;
		case 1:
			actual = bcm2712_clock_acquire(&core, 1, 240000000);
			expected = bcm2712_hsm_acquire(&reference, 1, 240000000);
			break;
		case 2:
			actual = bcm2712_clock_update(&core, 0, 200000000);
			expected = bcm2712_hsm_update(&reference, 0, 200000000);
			break;
		case 3:
			actual = bcm2712_clock_release(&core, 1);
			expected = bcm2712_hsm_release(&reference, 1);
			break;
		case 4:
			actual = bcm2712_clock_release(&core, 0);
			expected = bcm2712_hsm_release(&reference, 0);
			break;
		default:
			actual = bcm2712_clock_fini(&core);
			expected = bcm2712_hsm_fini(&reference);
			break;
		}
		CHECK(actual == expected);
		hsm_equal(&core, &reference);
		trace_equal(&a, &b);
	}
}

static void
init_unchanged(struct firmware *f, enum bcm2712_clock_profile profile, int error)
{
	struct bcm2712_clock core, saved;

	memset(&core, 0xa5, sizeof(core));
	memcpy(&saved, &core, sizeof(saved));
	CHECK(bcm2712_clock_init(&core, profile, exchange, f) == error);
	CHECK(memcmp(&core, &saved, sizeof(core)) == 0);
	CHECK(f->sets == 0);
}

static void
initialization(void)
{
	struct bcm2712_clock core;
	struct firmware f;
	unsigned int profile, call, word;
	static const unsigned int bad_words[] = { 0, 1, 2, 3, 4, 5, 7 };
	static const uint32_t states[] = { 2, 3, 4, 0x80000001 };

	for (profile = 0; profile < 2; profile++) {
		for (call = 1; call <= 3 + profile; call++) {
			f = firmware_new(profile, 0, 0);
			f.fail_call = call;
			init_unchanged(&f, profile, EIO);
			for (word = 0; word < sizeof(bad_words) /
			    sizeof(bad_words[0]); word++) {
				f = firmware_new(profile, 0, 0);
				f.bad_call = call;
				f.bad_word = bad_words[word];
				f.bad_value = 0xdeadbeef;
				init_unchanged(&f, profile, EIO);
			}
		}
		for (word = 0; word < sizeof(states) / sizeof(states[0]); word++) {
			f = firmware_new(profile, states[word], 0);
			init_unchanged(&f, profile, word < 2 ? ENODEV : EIO);
		}
	}
	f = firmware_new(BCM2712_CLOCK_BVB, 0, 0);
	f.minimum = 0;
	init_unchanged(&f, BCM2712_CLOCK_BVB, ERANGE);
	f = firmware_new(BCM2712_CLOCK_BVB, 1, 120000000);
	f.minimum = f.maximum + 1;
	init_unchanged(&f, BCM2712_CLOCK_BVB, ERANGE);
	f = firmware_new(BCM2712_CLOCK_HSM, 1, 300000001);
	init_unchanged(&f, BCM2712_CLOCK_HSM, ERANGE);
	f = firmware_new(BCM2712_CLOCK_BVB, 0, 0);
	init_unchanged(&f, (enum bcm2712_clock_profile)-1, EINVAL);
	init_unchanged(&f, (enum bcm2712_clock_profile)2, EINVAL);
	CHECK(f.calls == 0);
	CHECK(bcm2712_clock_init(NULL, BCM2712_CLOCK_BVB, exchange, &f) == EINVAL);
	CHECK(bcm2712_clock_init(&core, BCM2712_CLOCK_BVB, NULL, &f) == EINVAL);
	CHECK(f.calls == 0);
}

static void
lease_cases(enum bcm2712_clock_profile profile, uint32_t initial_state,
    uint32_t initial_rate)
{
	struct firmware f = firmware_new(profile, initial_state, initial_rate);
	struct bcm2712_clock core;
	struct bcm2712_clock_status status;
	uint32_t floor = profile == BCM2712_CLOCK_HSM ?
	    MAX(BCM2712_DISPLAY_HSM_MIN_RATE, initial_rate) : f.minimum;
	unsigned int before;

	CHECK(bcm2712_clock_init(&core, profile, exchange, &f) == 0);
	CHECK(core.status.floor == floor && f.sets == 0);
	CHECK(bcm2712_clock_acquire(&core, 0, 75000000) == 0);
	CHECK(core.status.rate == MAX(floor, 75000000));
	CHECK(core.status.state == 1 && core.status.leases == 1);
	before = f.calls;
	CHECK(bcm2712_clock_acquire(&core, 0, 100000000) == EBUSY);
	CHECK(bcm2712_clock_acquire(&core, 2, 100000000) == EINVAL);
	CHECK(bcm2712_clock_acquire(&core, 1, 0) == EINVAL);
	CHECK(bcm2712_clock_update(&core, 1, 100000000) == ENOENT);
	CHECK(bcm2712_clock_release(&core, 1) == ENOENT);
	CHECK(bcm2712_clock_release(&core, 2) == EINVAL);
	CHECK(bcm2712_clock_update(&core, 2, 100000000) == EINVAL);
	CHECK(bcm2712_clock_update(&core, 0, 0) == EINVAL);
	CHECK(bcm2712_clock_fini(&core) == EBUSY);
	CHECK(f.calls == before);
	CHECK(bcm2712_clock_acquire(&core, 1, 150000000) == 0);
	CHECK(core.status.rate == MAX(floor, 150000000));
	CHECK(bcm2712_clock_update(&core, 0, 300000000) == 0);
	CHECK(core.status.rate == 300000000);
	CHECK(bcm2712_clock_release(&core, 0) == 0);
	CHECK(core.status.leases == 2 && core.status.rate == MAX(floor, 150000000));
	f.maximum = 100000000;
	before = f.sets;
	CHECK(bcm2712_clock_update(&core, 1, 200000000) == ERANGE);
	CHECK(f.sets == before && core.status.minimum[1] == 150000000);
	CHECK(core.status.leases == 2 && core.status.fault == 0);
	f.maximum = 300000000;
	CHECK(bcm2712_clock_release(&core, 1) == 0);
	CHECK(core.status.leases == 0 && core.status.rate == floor);
	before = f.calls;
	CHECK(bcm2712_clock_status(&core, &status) == 0);
	CHECK(status.rate == floor && f.calls == before);
	CHECK(bcm2712_clock_status(&core, NULL) == EINVAL);
	CHECK(bcm2712_clock_fini(&core) == 0);
	CHECK(!core.initialized);
	CHECK(f.state == (profile == BCM2712_CLOCK_HSM ? initial_state : 1));
	CHECK(f.disables == (unsigned int)(profile == BCM2712_CLOCK_HSM &&
	    initial_state == 0));
	memset(&status, 0xa5, sizeof(status));
	CHECK(bcm2712_clock_status(&core, &status) == ENXIO);
	CHECK(status.rate == 0xa5a5a5a5);
	CHECK(bcm2712_clock_acquire(&core, 0, 75000000) == ENXIO);
	CHECK(bcm2712_clock_fini(&core) == ENXIO);
	CHECK(bcm2712_clock_status(NULL, &status) == ENXIO);
}

static void
operation_failure(enum bcm2712_clock_profile profile, unsigned int operation,
    unsigned int step, int corrupt_word)
{
	struct firmware f = firmware_new(profile, 0, 0);
	struct bcm2712_clock core;
	struct bcm2712_clock_status saved, observed;
	unsigned int base, before;
	int error;

	CHECK(bcm2712_clock_init(&core, profile, exchange, &f) == 0);
	if (operation != 0)
		CHECK(bcm2712_clock_acquire(&core, 0, 150000000) == 0);
	saved = core.status;
	base = f.calls;
	if (corrupt_word >= 0) {
		f.bad_call = base + step;
		f.bad_word = (unsigned int)corrupt_word;
		f.bad_value = 99;
	} else {
		f.fail_call = base + step;
		f.fail_after_set = true;
	}
	if (operation == 0)
		error = bcm2712_clock_acquire(&core, 0, 150000000);
	else if (operation == 1)
		error = bcm2712_clock_update(&core, 0, 200000000);
	else
		error = bcm2712_clock_release(&core, 0);
	CHECK(error == EIO);
	CHECK(core.status.rate == saved.rate && core.status.state == saved.state);
	CHECK(core.status.leases == saved.leases);
	CHECK(memcmp(core.status.minimum, saved.minimum,
	    sizeof(saved.minimum)) == 0);
	CHECK(core.status.fault == (step <= 3 ? 0 : EIO));
	CHECK(core.status.mutation_attempted == (saved.mutation_attempted || step > 3));
	before = f.calls;
	CHECK(bcm2712_clock_status(&core, &observed) == 0);
	CHECK(observed.fault == core.status.fault && f.calls == before);
	if (step > 3) {
		CHECK(bcm2712_clock_acquire(&core, 1, 75000000) == EIO);
		CHECK(bcm2712_clock_update(&core, 0, 75000000) == EIO);
		CHECK(bcm2712_clock_release(&core, 0) == EIO);
		CHECK(bcm2712_clock_fini(&core) == EIO);
		CHECK(core.initialized && f.calls == before);
	}
	CHECK(profile != BCM2712_CLOCK_BVB || f.disables == 0);
}

static void
readbacks(enum bcm2712_clock_profile profile)
{
	struct firmware f;
	struct bcm2712_clock core;
	unsigned int step, which;
	static const unsigned int steps[] = { 4, 5, 6, 7, 8 };

	f = firmware_new(profile, 0, 0);
	f.round = 1000000;
	CHECK(bcm2712_clock_init(&core, profile, exchange, &f) == 0);
	CHECK(bcm2712_clock_acquire(&core, 0, 150000000) == 0);
	CHECK(core.status.rate == 151000000 && core.status.leases == 1);
	CHECK(bcm2712_clock_release(&core, 0) == 0);
	CHECK(core.status.rate == core.status.floor + f.round);
	CHECK(bcm2712_clock_fini(&core) == 0);
	for (which = 0; which < 2; which++) {
		for (step = 0; step < sizeof(steps) / sizeof(steps[0]); step++) {
			f = firmware_new(profile, 0, 0);
			CHECK(bcm2712_clock_init(&core, profile, exchange, &f) == 0);
			f.bad_call = f.calls + steps[step];
			f.bad_word = 6;
			f.bad_value = which == 0 ? 0 : f.maximum + 1;
			CHECK(bcm2712_clock_acquire(&core, 0, 150000000) != 0);
			CHECK(core.status.fault != 0 && core.status.leases == 0);
			CHECK(core.status.minimum[0] == 0 && core.status.state == 0);
		}
	}
	f = firmware_new(profile, 0, 0);
	CHECK(bcm2712_clock_init(&core, profile, exchange, &f) == 0);
	f.round = f.maximum;
	CHECK(bcm2712_clock_acquire(&core, 0, 150000000) == ERANGE);
	CHECK(core.status.fault == ERANGE && core.status.leases == 0);
}

static void
no_set_failures(enum bcm2712_clock_profile profile)
{
	struct firmware f;
	struct bcm2712_clock core;
	unsigned int step, before;

	for (step = 1; step <= 5; step++) {
		f = firmware_new(profile, 1, 150000000);
		CHECK(bcm2712_clock_init(&core, profile, exchange, &f) == 0);
		f.fail_call = f.calls + step;
		CHECK(bcm2712_clock_acquire(&core, 0, 150000000) == EIO);
		CHECK(f.sets == 0 && core.status.fault == 0);
		CHECK(!core.status.mutation_attempted && core.status.leases == 0);
		before = f.calls;
		CHECK(bcm2712_clock_fini(&core) == 0 && f.calls == before);
	}
	f = firmware_new(profile, 0, 0);
	CHECK(bcm2712_clock_init(&core, profile, exchange, &f) == 0);
	CHECK(bcm2712_clock_acquire(&core, 0, 150000000) == 0);
	f.maximum = core.status.floor - 1;
	before = f.sets;
	CHECK(bcm2712_clock_release(&core, 0) == ERANGE && f.sets == before);
	CHECK(core.status.leases == 1 && core.status.fault == 0);
	CHECK(bcm2712_clock_fini(&core) == EBUSY);
}

static void
finish_failures(void)
{
	struct firmware f;
	struct bcm2712_clock core;
	unsigned int step, bad, before;

	for (step = 1; step <= 2; step++) {
		for (bad = 0; bad <= 1; bad++) {
			f = firmware_new(BCM2712_CLOCK_HSM, 0, 0);
			CHECK(bcm2712_clock_init(&core, BCM2712_CLOCK_HSM, exchange, &f) == 0);
			CHECK(bcm2712_clock_acquire(&core, 0, 120000000) == 0);
			CHECK(bcm2712_clock_release(&core, 0) == 0);
			if (bad) {
				f.bad_call = f.calls + step;
				f.bad_word = 6;
				f.bad_value = 1;
			} else {
				f.fail_call = f.calls + step;
				f.fail_after_set = true;
			}
			CHECK(bcm2712_clock_fini(&core) == EIO);
			CHECK(core.status.fault == EIO && core.initialized);
			before = f.calls;
			CHECK(bcm2712_clock_fini(&core) == EIO && f.calls == before);
		}
	}
	f = firmware_new(BCM2712_CLOCK_BVB, 0, 0);
	CHECK(bcm2712_clock_init(&core, BCM2712_CLOCK_BVB, exchange, &f) == 0);
	before = f.calls;
	CHECK(bcm2712_clock_fini(&core) == 0);
	CHECK(f.calls == before && f.sets == 0 && f.state == 0);
}

int
main(void)
{
	unsigned int profile, state, rate, failure, bad, operation, step, word;
	static const uint32_t initial_rates[] = { 0, 120000000, 240000000 };
	static const unsigned int fields[] = { 0, 1, 2, 3, 4, 5, 7 };

	initialization();
	for (state = 0; state <= 1; state++) {
		for (rate = 0; rate < sizeof(initial_rates) / sizeof(initial_rates[0]); rate++) {
			for (failure = 0; failure <= 45; failure++) {
				for (bad = 0; bad <= 1; bad++)
					hsm_trace(state, initial_rates[rate], failure, bad);
			}
		}
	}
	for (profile = 0; profile < 2; profile++) {
		for (state = 0; state <= 1; state++) {
			lease_cases(profile, state, 0);
			lease_cases(profile, state, 240000000);
		}
		for (operation = 0; operation < 3; operation++) {
			for (step = 1; step <= (operation == 0 ? 8U : 7U); step++) {
				operation_failure(profile, operation, step, -1);
				for (word = 0; word < sizeof(fields) / sizeof(fields[0]); word++) {
					unsigned int field = fields[word];
					if (field == 7 && step == 4)
						field = 8;
					operation_failure(profile, operation, step, (int)field);
				}
			}
		}
		readbacks(profile);
		no_set_failures(profile);
	}
	finish_failures();
	printf("BCM2712 clock core: %u checks passed\n", checks);
	return 0;
}
