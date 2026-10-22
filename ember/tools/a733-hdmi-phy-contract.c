/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted independent PHY and failure oracles. */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sun60i_a733_hdmi_phy.h"

static unsigned checks;
#define CHECK(x) do { \
	if (!(x)) { \
		fprintf(stderr, "line %d: %s\n", __LINE__, #x); \
		exit(1); \
	} \
	checks++; \
} while (0)

struct fixture {
	struct sun60i_a733_hdmi_phy state;
	struct sun60i_a733_hdmi_io io;
	uint32_t top[17];
	uint8_t core[0x5000];
	uint16_t internal[256];
	unsigned calls, writes, fail_at, ready_calls, delay_us;
	unsigned reset_count, reset_values[2];
	unsigned i2c_fail, top_timeout, phy_timeout, corrupt_readback;
	unsigned corrupt_address, read_i2c_fail;
	bool drop_level_shift;
	int ready_error;
	bool owned;
};

static int
ready(void *arg, const void *owner)
{
	struct fixture *f = arg;
	f->ready_calls++;
	CHECK(f->state.fault == 0);
	if (!f->owned || owner != f)
		return EPERM;
	return f->ready_error;
}

static int
read_reg(void *arg, enum sun60i_a733_hdmi_space space,
    uint32_t offset, uint32_t *out)
{
	struct fixture *f = arg;
	CHECK(f->owned && f->state.fault == 0);
	f->calls++;
	*out = 0xeeeeeeee;
	if (f->calls == f->fail_at)
		return EFAULT;
	if (space == A733_HDMI_CORE8) {
		CHECK(offset < sizeof(f->core));
		*out = f->core[offset];
		if (offset == 0x3004 && f->phy_timeout)
			*out &= ~1U;
	} else {
		CHECK(space == A733_HDMI_TOP32);
		CHECK((offset & 3) == 0 && offset <= 0x40);
		*out = f->top[offset / 4];
		if (offset == 0x40 && f->top_timeout)
			*out = 0;
	}
	return 0;
}

static int
write_reg(void *arg, enum sun60i_a733_hdmi_space space,
    uint32_t offset, uint32_t value)
{
	struct fixture *f = arg;
	CHECK(f->owned && f->state.fault == 0 && f->state.written);
	f->calls++;
	f->writes++;
	if (f->calls == f->fail_at)
		return EFAULT;
	if (space == A733_HDMI_CORE8) {
		CHECK(offset < sizeof(f->core) && value <= 255);
		if (offset == 0x0108) {
			f->core[offset] &= ~(uint8_t)value;
			return 0;
		}
		f->core[offset] = value;
		if (offset == 0x4005) {
			CHECK(f->reset_count < 2);
			f->reset_values[f->reset_count++] = value & 1;
		}
		if (offset == 0x3026) {
			unsigned address = f->core[0x3021];
			CHECK((f->core[0x4005] & 1) == 0);
			CHECK((f->core[0x3000] & 0x20) != 0);
			CHECK((f->core[0x3034] & 0x11) == 0x10);
			if (value == 0x10) {
				f->internal[address] = (uint16_t)((f->core[0x3022] << 8) |
				    f->core[0x3023]);
			} else {
				uint16_t data = f->internal[address];

				CHECK(value == 1);
				if (f->corrupt_address == 0 || f->corrupt_address == address)
					data ^= f->corrupt_readback;
				f->core[0x3024] = data >> 8;
				f->core[0x3025] = data & 255;
			}
			if (value == 1 && f->read_i2c_fail != 0) {
				f->core[0x0108] = f->read_i2c_fail == 1 ? 1 : 0;
				return 0;
			}
			f->core[0x0108] = f->i2c_fail == 1 ? 1 :
			    f->i2c_fail == 2 ? 0 : f->i2c_fail == 3 ? 3 : 2;
		}
	} else {
		CHECK(space == A733_HDMI_TOP32);
		CHECK((offset & 3) == 0 && offset <= 0x40);
		if (!(offset == 0x24 && f->drop_level_shift && (value & 0x80)))
			f->top[offset / 4] = value;
	}
	return 0;
}

static void
wait_us(void *arg, unsigned us)
{
	struct fixture *f = arg;
	CHECK(f->state.fault == 0);
	f->delay_us += us;
}

static void
fresh(struct fixture *f)
{
	memset(f, 0, sizeof(*f));
	f->io = (struct sun60i_a733_hdmi_io){ f, ready, read_reg, write_reg, wait_us };
	f->state.io = &f->io;
	f->state.owner = f;
	f->owned = true;
	f->top[0] = 0x40000000; /* Unrelated bit must survive. */
	f->top[0x40 / 4] = 1;
	f->core[0x3004] = 3;
	f->core[0x3000] = 0xc0; /* C422 spare bits must survive. */
	f->core[0x3034] = 0x81; /* Hold JTAG reset; preserve its spare bits. */
	f->internal[0x15] = 0xdead; /* The old PHY register must not be used. */
}

static int
complete(struct fixture *f)
{
	int error;

	error = sun60i_a733_hdmi_hpd_prepare(&f->state);
	if (error == 0) {
		CHECK((f->core[0x3000] & 0x1f) == 0x16);
		CHECK(f->internal[6] == 0);
		error = sun60i_a733_hdmi_phy_prepare(&f->state, 26000000, 108000000);
	}
	if (error == 0)
		error = sun60i_a733_hdmi_phy_observe(&f->state);
	return error;
}

static void
plans_test(void)
{
	const uint32_t clocks[][2] = {
		{ 24000000, 27000000 }, { 24000000, 54000000 }, { 24000000, 74250000 },
		{ 24000000, 148500000 }, { 26000000, 54000000 }, { 26000000, 108000000 }
	};
	struct sun60i_a733_hdmi_plan p, before;
	for (unsigned i = 0; i < sizeof(clocks) / sizeof(clocks[0]); i++) {
		CHECK(sun60i_a733_hdmi_plan(clocks[i][0], clocks[i][1], &p) == 0);
		CHECK((uint64_t)clocks[i][0] * (((p.pll >> 8) & 255) + 1) ==
		    (uint64_t)clocks[i][1] * (((p.pll >> 1) & 1) + 1) *
		    (((p.pll >> 16) & 127) + 1));
	}
	memset(&before, 0xa5, sizeof(before));
	p = before;
	CHECK(sun60i_a733_hdmi_plan(26000000, 74250000, &p) == EOPNOTSUPP);
	CHECK(memcmp(&p, &before, sizeof(p)) == 0);
	CHECK(sun60i_a733_hdmi_plan(26000000, 27000000, &p) == EOPNOTSUPP);
	CHECK(sun60i_a733_hdmi_plan(19200000, 108000000, &p) == EOPNOTSUPP);
	CHECK(sun60i_a733_hdmi_plan(26000000, 108000001, &p) == EOPNOTSUPP);
	CHECK(sun60i_a733_hdmi_plan(26000000, 108000000, NULL) == EINVAL);
}

static void
normal_and_faults(void)
{
	struct fixture f;
	unsigned count;
	fresh(&f);
	CHECK(complete(&f) == 0);
	CHECK(f.state.hpd_prepared && f.state.phy_prepared && f.state.written);
	CHECK(f.top[0] == 0x40000017);
	CHECK(f.top[0x20 / 4] == 0xe80c3500);
	CHECK(f.top[0x28 / 4] == 0x35000 && f.top[0x30 / 4] == 0x30000000);
	CHECK(f.internal[0x06] == 1 && f.internal[0x10] == 0x20c0);
	CHECK(f.internal[0x11] == 0x060a && f.internal[0x15] == 0xdead);
	CHECK(f.internal[0x19] == 7 && f.internal[0x0e] == 0x8160);
	CHECK(f.internal[0x09] == 0x8188);
	CHECK(f.reset_count == 2 && f.reset_values[0] == 1 && f.reset_values[1] == 0);
	CHECK(f.core[0x3000] == 0xee);
	CHECK(f.core[0x3034] == 0x90);
	count = f.calls;
	CHECK(sun60i_a733_hdmi_hpd_prepare(&f.state) == EALREADY);
	CHECK(sun60i_a733_hdmi_phy_prepare(&f.state, 26000000, 108000000) == EALREADY);
	CHECK(f.calls == count);
	for (unsigned failure = 1; failure <= count; failure++) {
		unsigned calls;

		fresh(&f);
		f.fail_at = failure;
		CHECK(complete(&f) == EFAULT && f.state.fault == EFAULT);
		CHECK(f.calls == failure);
		calls = f.calls;
		CHECK(sun60i_a733_hdmi_hpd_prepare(&f.state) == EFAULT);
		CHECK(sun60i_a733_hdmi_phy_prepare(&f.state, 26000000, 108000000) == EFAULT);
		CHECK(sun60i_a733_hdmi_phy_observe(&f.state) == EFAULT);
		CHECK(f.calls == calls);
	}
	fresh(&f);
	f.owned = false;
	CHECK(complete(&f) == EPERM && f.calls == 0 && !f.state.written);
	fresh(&f);
	f.ready_error = EAGAIN;
	CHECK(complete(&f) == EAGAIN && f.calls == 0);
	fresh(&f);
	f.top[0] |= 0x80000000;
	CHECK(complete(&f) == EBUSY && f.writes == 0);
	fresh(&f);
	CHECK(complete(&f) == 0);
	f.top[0x20 / 4] ^= 0x100;
	CHECK(sun60i_a733_hdmi_phy_observe(&f.state) == EIO);
}

static void
timeouts(void)
{
	struct fixture f;
	fresh(&f);
	f.top_timeout = 1;
	CHECK(complete(&f) == ETIMEDOUT && f.delay_us == 20000);
	CHECK(!f.state.phy_prepared && f.internal[6] == 0);
	fresh(&f);
	f.i2c_fail = 1;
	CHECK(complete(&f) == EIO && !f.state.phy_prepared);
	fresh(&f);
	f.i2c_fail = 3;
	CHECK(complete(&f) == EIO && !f.state.phy_prepared);
	CHECK(f.core[0x0108] == 3); /* Error wins; no clear after failure. */
	fresh(&f);
	f.drop_level_shift = true;
	CHECK(complete(&f) == EIO && !f.state.phy_prepared);
	CHECK(f.internal[6] == 0);
	fresh(&f);
	f.i2c_fail = 2;
	CHECK(complete(&f) == ETIMEDOUT && f.delay_us == 5045);
	fresh(&f);
	f.phy_timeout = 1;
	CHECK(complete(&f) == ETIMEDOUT && f.delay_us == 2665);
	fresh(&f);
	f.corrupt_readback = 1;
	CHECK(complete(&f) == EIO && !f.state.phy_prepared);
	fresh(&f);
	f.core[0x3004] = 1;
	CHECK(complete(&f) == ENXIO && !f.state.phy_prepared);
}

static void
invalid_sequence(void)
{
	struct fixture f;
	unsigned calls;
	fresh(&f);
	CHECK(sun60i_a733_hdmi_phy_prepare(&f.state, 26000000, 108000000) == EINVAL);
	CHECK(f.state.fault == EINVAL && f.calls == 0);
	CHECK(sun60i_a733_hdmi_hpd_prepare(&f.state) == EINVAL && f.calls == 0);
	fresh(&f);
	CHECK(sun60i_a733_hdmi_hpd_prepare(&f.state) == 0);
	calls = f.calls;
	CHECK(sun60i_a733_hdmi_phy_prepare(&f.state, 26000000, 74250000) == EOPNOTSUPP);
	CHECK(sun60i_a733_hdmi_phy_prepare(&f.state, 26000000, 108000000) == EOPNOTSUPP);
	CHECK(f.calls == calls);
	fresh(&f);
	f.io.read = NULL;
	CHECK(sun60i_a733_hdmi_hpd_prepare(&f.state) == EINVAL);
	CHECK(f.state.fault == EINVAL && f.calls == 0);
	CHECK(sun60i_a733_hdmi_hpd_prepare(NULL) == EINVAL);
}

static void
comparison_trace(void)
{
	const struct {
		unsigned slot, address, requested, mask;
	} cases[] = {
		{ 0, 6, 1, 1 }, { 1, 0x10, 0x20c0, 1 },
		{ 2, 0x11, 0x060a, 1 }, { 3, 0x19, 7, 1 },
		{ 4, 0x0e, 0x8160, 1 }, { 5, 9, 0x8188, 1 },
		{ 4, 0x0e, 0x8160, 0x4000 }, { 5, 9, 0x8188, 0x8000 },
		{ 0, 6, 1, 0x8000 }, { 3, 0x19, 7, 0x8000 }
	};
	struct sun60i_a733_hdmi_phy_trace zero = { 0 };
	struct fixture f;

	CHECK(sizeof(zero) == 28);
	fresh(&f);
	CHECK(memcmp(&f.state.trace, &zero, sizeof(zero)) == 0);
	CHECK(complete(&f) == 0);
	CHECK(f.state.trace.version == 1 && f.state.trace.valid == 15);
	CHECK(f.state.trace.index == 5 && f.state.trace.address == 9);
	CHECK(f.state.trace.requested == 0x8188 && f.state.trace.observed == 0x8188);
	CHECK(f.state.trace.status == 2);
	for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		struct sun60i_a733_hdmi_phy_trace saved;
		unsigned calls;

		fresh(&f);
		f.corrupt_address = cases[i].address;
		f.corrupt_readback = cases[i].mask;
		CHECK(complete(&f) == EIO);
		CHECK(f.state.trace.version == 1 && f.state.trace.valid == 15);
		CHECK(f.state.trace.index == cases[i].slot &&
		    f.state.trace.address == cases[i].address);
		CHECK(f.state.trace.requested == cases[i].requested);
		CHECK(f.state.trace.observed == (cases[i].requested ^ cases[i].mask));
		CHECK(f.state.trace.status == 2 && !f.state.phy_prepared);
		saved = f.state.trace;
		calls = f.calls;
		CHECK(sun60i_a733_hdmi_phy_observe(&f.state) == EIO);
		CHECK(sun60i_a733_hdmi_phy_prepare(&f.state,
		    26000000, 108000000) == EIO);
		CHECK(f.calls == calls &&
		    memcmp(&saved, &f.state.trace, sizeof(saved)) == 0);
	}
	/* VLEVCTRL bit 15 is written but does not read back. */
	fresh(&f);
	f.corrupt_address = 0x0e;
	f.corrupt_readback = 0x8000;
	CHECK(complete(&f) == 0 && f.state.phy_prepared);
	CHECK(f.internal[0x0e] == 0x8160 && f.internal[0x09] == 0x8188);
	CHECK(f.state.trace.version == 1 && f.state.trace.valid == 15);
	CHECK(f.state.trace.index == 5 && f.state.trace.address == 9);
	CHECK(f.state.trace.requested == 0x8188 && f.state.trace.observed == 0x8188);
	for (unsigned i = 1; i <= 2; i++) {
		fresh(&f);
		f.read_i2c_fail = i;
		CHECK(complete(&f) == (i == 1 ? EIO : ETIMEDOUT));
		CHECK(f.state.trace.version == 1 && f.state.trace.valid == 3);
		CHECK(f.state.trace.index == 0 && f.state.trace.address == 6);
		CHECK(f.state.trace.requested == 1 && f.state.trace.observed == 0);
		CHECK(f.state.trace.status == (i == 1 ? 1U : 0U));
	}
}

int
main(void)
{
	comparison_trace();
	plans_test();
	normal_and_faults();
	timeouts();
	invalid_sequence();
	printf("%u A733 PHY checks passed\n", checks);
	return 0;
}
