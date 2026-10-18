/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted; actual native ownership production code. */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef unsigned int u_int;
#define __BIT(n) (UINT32_C(1) << (n))
#define __BITS(h,l) ((UINT32_MAX >> (31 - (h))) & (UINT32_MAX << (l)))
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#include "sun60i_a733_display.h"
#include "sun60i_a733_display_owner.h"
#include "sun60i_a733_display.c"
#include "sun60i_a733_display_owner.c"

static unsigned checks;
#define CHECK(c) do { const bool ok = (c); assert(ok); checks++; } while (0)

struct fixture {
	struct sun60i_a733_display_owner state;
	struct sun60i_a733_display_inputs input;
	unsigned reads;
	int error;
	bool reenter;
};

static void
init(struct fixture *f)
{
	memset(f, 0, sizeof(*f));
	for (unsigned s = 0; s < 2; s++) {
		uint32_t *c = f->input.clocks.sample[s];
		uint32_t *a = f->input.ancestor[s];
		/* Actual stable observer shape; borrowed ancestors below are synthetic. */
		static const uint32_t boot[] = {
			0x4d113100, 0x110103ff, 0xf1055803, 0, 0, 0x10000,
			0x10001, 0, 0, 0x30001, 0x80000000, 0x10001, 0x10000,
			0x30003, 0x30000, 0, 0x30000000, 0xc3000000, 0, 0,
			0, 0x80000003, 3
		};
		memcpy(c, boot, sizeof(boot));
		for (unsigned d = 0; d < 2; d++) {
			f->input.power.sample[s][d][0] = 8;
			f->input.power.sample[s][d][2] = 8;
		}
		/* Independent raw register vectors: 26MHz -> 24MHz REF. */
		a[0] = 0xf8675f00;
		a[1] = 0xf8003100; /* 24MHz * 50 = 1200MHz. */
		a[4] = 0x03000002; /* 600MHz / 3 = 200MHz AHB. */
		a[5] = 0x02060206; /* Manual + configured 400/400_ALL/600. */
		a[6] = 0x02060000;
		f->input.dcxo[s][0] = f->input.dcxo[s][1] = 0x8000;
	}
}

static int
read_inputs(void *arg, struct sun60i_a733_display_inputs *out)
{
	struct fixture *f = arg;
	f->reads++;
	if (f->reenter) {
		CHECK(sun60i_a733_display_writer_enter(&f->state,
		    A733_DISPLAY_RES_OTHER) == EBUSY);
		CHECK(sun60i_a733_display_reserve(&f->state, f, read_inputs, f) == EBUSY);
	}
	if (f->error != 0)
		return f->error;
	*out = f->input;
	return 0;
}

static int
reserve(struct fixture *f)
{
	return sun60i_a733_display_reserve(&f->state, f, read_inputs, f);
}

static int
begin(struct fixture *f)
{
	return sun60i_a733_display_begin_de0(&f->state, f, read_inputs, f);
}

static void
ancestors(void)
{
	struct fixture f;
	struct sun60i_a733_display_rates rates, sentinel = { 1, 2, 3 };
	init(&f);
	CHECK(sun60i_a733_display_running_ancestors(&f.input, &rates) == 0);
	CHECK(rates.reference_hz == 24000000 && rates.periph400_hz == 400000000 &&
	    rates.ahb_hz == 200000000);
	/* Every enable/lock/output flag in both borrowed PLLs matters. */
	for (unsigned r = 0; r < 2; r++) {
		for (unsigned b = 27; b < 32; b++) {
			init(&f);
			for (unsigned s = 0; s < 2; s++)
				f.input.ancestor[s][r] &= ~__BIT(b);
			rates = sentinel;
			CHECK(sun60i_a733_display_running_ancestors(&f.input, &rates) == EBUSY);
			CHECK(memcmp(&rates, &sentinel, sizeof(rates)) == 0);
		}
	}
	static const unsigned gate_bits[] = { 1, 2, 9, 17, 18, 25 };
	for (unsigned i = 0; i < __arraycount(gate_bits); i++) {
		init(&f);
		for (unsigned s = 0; s < 2; s++)
			f.input.ancestor[s][5] &= ~__BIT(gate_bits[i]);
		CHECK(sun60i_a733_display_running_ancestors(&f.input, &rates) == EBUSY);
	}
	for (unsigned i = 3; i < __arraycount(gate_bits); i++) {
		init(&f);
		for (unsigned s = 0; s < 2; s++)
			f.input.ancestor[s][6] &= ~__BIT(gate_bits[i]);
		CHECK(sun60i_a733_display_running_ancestors(&f.input, &rates) == EBUSY);
	}
	/* SDM, wrong REF normalization, non-integer/low/wrong PLL factors. */
	static const uint32_t bad[][2] = {
		{ 0, 0xf8000000 }, { 1, 0xf8000900 }, { 1, 0xf8003000 },
		{ 1, 0xf8700a02 }, { 2, 0x80000000 }, { 3, 0x08000000 },
		{ 3, 0x80000000 }, { 3, 0x04000000 }, { 3, 0x02000000 },
		{ 3, 0x01000000 }, { 3, 0x00100000 },
		{ 4, 0x01000000 }, { 4, 0x02000000 }, { 4, 0x03000006 }
	};
	for (unsigned i = 0; i < __arraycount(bad); i++) {
		init(&f);
		for (unsigned s = 0; s < 2; s++)
			f.input.ancestor[s][bad[i][0]] = bad[i][1];
		rates = sentinel;
		CHECK(sun60i_a733_display_running_ancestors(&f.input, &rates) == EOPNOTSUPP);
		CHECK(memcmp(&rates, &sentinel, sizeof(rates)) == 0);
	}
	/* Actual DCXO selection, not a hard-coded nominal board frequency. */
	static const uint32_t ref[] = { 0xf8000000, 0xf8030400, 0xf8675f00, 0xf8000000 };
	for (unsigned i = 0; i < 4; i++) {
		init(&f);
		for (unsigned s = 0; s < 2; s++) {
			f.input.dcxo[s][0] = f.input.dcxo[s][1] = i << 14;
			f.input.ancestor[s][0] = ref[i];
			f.input.ancestor[s][4] = 1; /* SYS24M / 2. */
			f.input.ancestor[s][5] &= ~0x02000200;
			f.input.ancestor[s][6] &= ~0x02000000;
		}
		CHECK(sun60i_a733_display_running_ancestors(&f.input, &rates) == 0);
		CHECK(rates.ahb_hz == 12000000);
	}
}

static void
arbitration(void)
{
	struct fixture f;
	struct sun60i_a733_display_owner_status status;
	init(&f);
	CHECK(sun60i_a733_display_writer_exit(&f.state) == EINVAL);
	CHECK(sun60i_a733_display_writer_enter(&f.state, -1) == EINVAL);
	f.state.writers = UINT_MAX;
	CHECK(sun60i_a733_display_writer_enter(&f.state, A733_DISPLAY_RES_OTHER) == EOVERFLOW);
	f.state.writers = 0;
	for (unsigned r = 0; r <= A733_DISPLAY_RES_OTHER; r++) {
		CHECK(sun60i_a733_display_writer_enter(&f.state, r) == 0);
		CHECK(reserve(&f) == EBUSY && f.reads == 0);
		CHECK(sun60i_a733_display_writer_exit(&f.state) == 0);
	}
	f.reenter = true;
	CHECK(reserve(&f) == 0);
	CHECK(reserve(&f) == EBUSY && f.reads == 1);
	CHECK(sun60i_a733_display_release(&f.state, &checks) == EINVAL);
	CHECK(sun60i_a733_display_begin_de0(&f.state, &checks, read_inputs, &f) == EINVAL);
	for (unsigned r = 0; r < A733_DISPLAY_RES_OTHER; r++)
		CHECK(sun60i_a733_display_writer_enter(&f.state, r) == EBUSY);
	CHECK(sun60i_a733_display_writer_enter(&f.state, A733_DISPLAY_RES_OTHER) == 0);
	CHECK(begin(&f) == EBUSY && f.reads == 1);
	CHECK(sun60i_a733_display_writer_exit(&f.state) == 0);
	CHECK(sun60i_a733_display_fault(&f.state, &f, EIO) == EINVAL);
	CHECK(begin(&f) == 0 && f.state.retained);
	CHECK(begin(&f) == EBUSY && f.reads == 2);
	CHECK(sun60i_a733_display_release(&f.state, &f) == EBUSY);
	CHECK(sun60i_a733_display_fault(&f.state, &f, 0) == EINVAL);
	CHECK(sun60i_a733_display_fault(&f.state, &checks, EIO) == EINVAL);
	CHECK(sun60i_a733_display_fault(&f.state, &f, EIO) == 0);
	CHECK(sun60i_a733_display_fault(&f.state, &f, EFAULT) == 0);
	CHECK(sun60i_a733_display_owner_status(&f.state, &status) == 0);
	CHECK(status.fault == EIO && status.reserved && status.retained && f.reads == 2);
	CHECK(status.rates.periph400_hz == 400000000);
}

static void
freshness(void)
{
	struct fixture f;
	/* Every snapshot word is compared, even with an incorrect zero mask. */
	for (unsigned kind = 0; kind < 4; kind++) {
		unsigned count = kind == 0 ? A733_DISPLAY_NCLOCK_REGS :
		    kind == 1 ? 6 : kind == 2 ? A733_DISPLAY_NANCESTORS : 2;
		for (unsigned r = 0; r < count; r++) {
			init(&f);
			uint32_t *p = kind == 0 ? f.input.clocks.sample[1] :
			    kind == 1 ? f.input.power.sample[1][r / 3] :
			    kind == 2 ? f.input.ancestor[1] : f.input.dcxo[1];
			p[kind == 1 ? r % 3 : r] ^= 1;
			CHECK(reserve(&f) == EBUSY && f.state.owner == NULL);
		}
	}
	for (unsigned kind = 0; kind < 4; kind++) {
		init(&f);
		CHECK(reserve(&f) == 0);
		for (unsigned s = 0; s < 2; s++) {
			switch (kind) {
			case 0: f.input.clocks.sample[s][0] ^= 1; break;
			case 1: f.input.power.sample[s][0][0] |= 0x1000; break;
			case 2: f.input.ancestor[s][2] ^= 1; break;
			case 3: f.input.dcxo[s][0] ^= 1; f.input.dcxo[s][1] ^= 1; break;
			}
		}
		CHECK(begin(&f) == EBUSY && !f.state.retained);
		CHECK(sun60i_a733_display_release(&f.state, &f) == 0);
	}
	for (unsigned phase = 0; phase < 2; phase++) {
		init(&f);
		if (phase != 0)
			CHECK(reserve(&f) == 0);
		f.error = EIO; /* Access error or quarantined provider. */
		CHECK((phase == 0 ? reserve(&f) : begin(&f)) == EIO);
		CHECK(!f.state.checking && !f.state.retained);
		if (phase != 0)
			CHECK(sun60i_a733_display_release(&f.state, &f) == 0);
	}
	init(&f);
	for (unsigned s = 0; s < 2; s++)
		f.input.ancestor[s][1] &= ~0x80000000;
	CHECK(reserve(&f) == 0); /* Reservation is not parent readiness. */
	CHECK(begin(&f) == EBUSY && !f.state.retained);
	CHECK(sun60i_a733_display_release(&f.state, &f) == 0);
	CHECK(f.state.owner == NULL && f.state.rates.periph400_hz == 0);
}

static void
unsafe_initial(void)
{
	struct fixture f;
	static const uint32_t bad[][2] = {
		{ 3, 0x80000000 }, { 4, 1 }, { 4, 0x10000 }, { 5, 0 },
		{ 12, 0 }, { 17, 0x08000000 }, { 17, 0x10000000 },
		{ 21, 0x08000000 }, { 1, 0x110103df }, { 1, 0x110103ef },
		{ 1, 0x910103ff }, { 2, 0xf1055003 }
	};
	for (unsigned i = 0; i < __arraycount(bad); i++) {
		init(&f);
		for (unsigned s = 0; s < 2; s++)
			f.input.clocks.sample[s][bad[i][0]] = bad[i][1];
		CHECK(reserve(&f) == EBUSY && f.state.owner == NULL);
	}
	init(&f);
	f.input.clocks.sample[0][3] = f.input.clocks.sample[1][3] = 0x07000000;
	CHECK(reserve(&f) == EOPNOTSUPP);
	for (unsigned d = 0; d < 2; d++) {
		for (unsigned r = 0; r < 3; r++) {
			init(&f);
			for (unsigned s = 0; s < 2; s++)
				f.input.power.sample[s][d][r] ^= 1;
			CHECK(reserve(&f) == EBUSY && f.state.owner == NULL);
		}
	}
	init(&f);
	f.input.clocks.changed = 1;
	CHECK(reserve(&f) == EBUSY);
	init(&f);
	f.input.power.changed = 1;
	CHECK(reserve(&f) == EBUSY);
}

int
main(void)
{
	ancestors();
	arbitration();
	freshness();
	unsafe_initial();
	printf("PASS: A733 display owner: %u checks; no hardware access\n", checks);
	return 0;
}
