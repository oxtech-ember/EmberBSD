/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted; independent timing and W0C oracles. */
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sun60i_a733_output.h"
#include "sun60i_a733_hdmi_phy.h"
#include "sun60i_a733_output.c"

static unsigned checks;
#define CHECK(c) do { if (!(c)) { \
	fprintf(stderr, "line %d: %s\n", __LINE__, #c); exit(1); \
} checks++; } while (0)

/* Serial test stand-ins for kernel locking, not a concurrency acceptance. */
typedef struct { bool held; } kmutex_t;
#define CTASSERT(c) _Static_assert(c, #c)
#define MUTEX_DEFAULT 0
#define IPL_NONE 0
#define KASSERT(c) CHECK(c)
#define ONCE_DECL(n) int n
#define RUN_ONCE(o, fn) do { if (*(o) == 0) { CHECK(fn() == 0); *(o) = 1; } } while (0)
static void mutex_init(kmutex_t *m, int a, int b) { (void)a; (void)b; m->held = false; }
static void mutex_enter(kmutex_t *m) { CHECK(!m->held); m->held = true; }
static void mutex_exit(kmutex_t *m) { CHECK(m->held); m->held = false; }
static bool mutex_owned(kmutex_t *m) { return m->held; }
#include "sun60i_a733_output_provider.c"

static const struct sun60i_a733_output_mode hd = {
	148500000, 1920, 2008, 2052, 2200, 1080, 1084, 1089, 1125, true, true
};

static void
reject(struct sun60i_a733_output_mode mode, int error)
{
	struct sun60i_a733_tcon_timing out, saved;

	memset(&out, 0xa5, sizeof(out));
	saved = out;
	CHECK(sun60i_a733_tcon_timing_encode(&mode, &out) == error);
	CHECK(memcmp(&out, &saved, sizeof(out)) == 0);
}

static void
timing(void)
{
	struct sun60i_a733_output_mode mode = hd;
	struct sun60i_a733_tcon_timing out;
	const struct sun60i_a733_tcon_timing expected = {
		2250, 0x077f0437, 0x089700bf, 40, 0x002b0004, 0x03000000
	};

	CHECK(sun60i_a733_tcon_timing_encode(&mode, &out) == 0);
	CHECK(memcmp(&out, &expected, sizeof(out)) == 0);
	mode = (struct sun60i_a733_output_mode){
		74250000, 1280, 1390, 1430, 1650, 720, 725, 730, 750, true, true
	};
	CHECK(sun60i_a733_tcon_timing_encode(&mode, &out) == 0);
	CHECK(out.basic1 == 1500 && out.basic2 == 0x04ff02cf);
	CHECK(out.basic3 == 0x06710103 && out.basic4 == 24);
	CHECK(out.basic5 == 0x00270004);
	mode.hsync_positive = false;
	CHECK(sun60i_a733_tcon_timing_encode(&mode, &out) == 0);
	CHECK(out.polarity == 0x01000000);
	mode.vsync_positive = false;
	CHECK(sun60i_a733_tcon_timing_encode(&mode, &out) == 0);
	CHECK(out.polarity == 0);
	mode.hsync_positive = true;
	CHECK(sun60i_a733_tcon_timing_encode(&mode, &out) == 0);
	CHECK(out.polarity == 0x02000000);

	mode = hd; mode.pixel_clock_hz = 0; reject(mode, EINVAL);
	mode = hd; mode.hdisplay = 0; reject(mode, EINVAL);
	mode = hd; mode.vdisplay = 0; reject(mode, EINVAL);
	mode = hd; mode.hsync_start = mode.hdisplay; reject(mode, EINVAL);
	mode = hd; mode.hsync_end = mode.hsync_start; reject(mode, EINVAL);
	mode = hd; mode.htotal = mode.hsync_end; reject(mode, EINVAL);
	mode = hd; mode.vsync_start = mode.vdisplay; reject(mode, EINVAL);
	mode = hd; mode.vsync_end = mode.vsync_start; reject(mode, EINVAL);
	mode = hd; mode.vtotal = mode.vsync_end; reject(mode, EINVAL);
	mode = hd; mode.pixel_clock_hz = UINT32_MAX; reject(mode, ERANGE);
	mode = hd; mode.htotal = UINT32_MAX; reject(mode, ERANGE);
	mode = hd; mode.vtotal = UINT32_MAX; reject(mode, ERANGE);
	mode = hd; mode.htotal = 65536; reject(mode, ERANGE); /* HBP overflow. */
	mode = hd; mode.vtotal = 65535; reject(mode, ERANGE); /* VBP overflow. */
	mode = hd; mode.hsync_end = 6105; mode.htotal = 6200;
	reject(mode, ERANGE);
	mode = hd; mode.vsync_end = 5181; mode.vtotal = 5200;
	reject(mode, ERANGE);
	mode = (struct sun60i_a733_output_mode){
		600000000, 32768, 32769, 32770, 65536,
		32768, 32769, 32770, 65535, false, false
	};
	CHECK(sun60i_a733_tcon_timing_encode(&mode, &out) == 0);
	CHECK(out.basic1 == 131070 && out.basic2 == 0x7fff7fff);
	CHECK(out.basic3 == 0xffff7ffe && out.basic4 == 32765);
	mode.hdisplay = 32769; mode.hsync_start = 32770;
	mode.hsync_end = 32771; reject(mode, ERANGE);
	mode = hd;
	CHECK(sun60i_a733_tcon_timing_encode(NULL, &out) == EINVAL);
	CHECK(sun60i_a733_tcon_timing_encode(&mode, NULL) == EINVAL);
}

static void
vblank(void)
{
	uint32_t ack = 0xa5a5a5a5, flags;

	CHECK(sun60i_a733_tcon_vblank_ack(0, &ack) == ENOENT);
	CHECK(ack == 0xa5a5a5a5);
	CHECK(sun60i_a733_tcon_vblank_ack(0x40000000, &ack) == ENOENT);
	CHECK(sun60i_a733_tcon_vblank_ack(0x00004000, &ack) == ENOENT);
	CHECK(ack == 0xa5a5a5a5);
	CHECK(sun60i_a733_tcon_vblank_ack(0x40004000, &ack) == 0);
	CHECK(ack == 0x40001f00);
	/* Model an unrelated line interrupt that arrives after the snapshot. */
	flags = 0x00004200;
	flags &= ack & 0x00005f00;
	CHECK(flags == 0x00000200);
	CHECK(sun60i_a733_tcon_vblank_ack(0xffffffff, &ack) == 0);
	CHECK(ack == 0x5f001f00);
	CHECK(sun60i_a733_tcon_vblank_ack(0x40004000, NULL) == EINVAL);
}

struct provider_fixture {
	unsigned calls[5];
	unsigned fail_step;
	bool no_vblank, invalid_mode;
	const void *owner;
};

static int
provider_step(void *arg, const void *owner, unsigned step)
{
	struct provider_fixture *f = arg;

	CHECK(mutex_owned(&output_lock));
	CHECK(owner == f->owner);
	f->calls[step]++;
	return f->fail_step == step + 1 ? EIO : 0;
}

static int
provider_mode(void *arg, const void *owner, struct sun60i_a733_output_mode *out)
{
	struct provider_fixture *f = arg;

	*out = hd;
	if (f->invalid_mode)
		out->pixel_clock_hz = 0;
	return provider_step(arg, owner, 0);
}

static int
provider_prepare(void *arg, const void *owner,
    const struct sun60i_a733_output_mode *mode)
{
	CHECK(mode->pixel_clock_hz == hd.pixel_clock_hz);
	return provider_step(arg, owner, 1);
}

static int
provider_start(void *arg, const void *owner)
{
	return provider_step(arg, owner, 2);
}

static int
provider_vblank(void *arg, const void *owner, uint64_t *out)
{
	struct provider_fixture *f = arg;
	int error = provider_step(arg, owner, 3);

	*out = 99; /* A failing callback must not publish even this value. */
	return error != 0 ? error : f->no_vblank ? ENOENT : 0;
}

static void
provider_fault(void *arg, const void *owner, int error)
{
	CHECK(error == EIO || error == EINVAL || error == ETIMEDOUT);
	(void)provider_step(arg, owner, 4);
}

static const struct sun60i_a733_output_ops provider_ops = {
	provider_mode, provider_prepare, provider_start, provider_vblank,
	provider_fault, NULL, NULL
};

static void
provider_reset(void)
{
	/* Reset only the test process. The kernel has no reset or detach API. */
	output_phandle = 0;
	output_ops = NULL;
	output_arg = NULL;
	output_owner = NULL;
	output_mode_valid = output_prepared = output_started = false;
	output_error = 0;
}

static void
provider(void)
{
	struct sun60i_a733_output_mode mode, changed;
	struct sun60i_a733_output_ops ops = provider_ops;
	struct provider_fixture f = { .owner = &f };
	uint64_t sequence = 123;

	CHECK(sun60i_a733_output_get_mode(7, &f, &mode) == ENXIO);
	CHECK(sun60i_a733_output_start(7, &f) == ENXIO);
	CHECK(sun60i_a733_output_register(0, &ops, &f) == EINVAL);
	ops.start = NULL;
	CHECK(sun60i_a733_output_register(7, &ops, &f) == EINVAL);
	CHECK(sun60i_a733_output_register(7, &provider_ops, &f) == 0);
	CHECK(sun60i_a733_output_register(7, &provider_ops, &f) == EBUSY);
	CHECK(sun60i_a733_output_prepare(7, &f, &hd) == EINVAL);
	CHECK(sun60i_a733_output_start(7, &f) == EINVAL);
	CHECK(sun60i_a733_output_fault(7, &f, EIO) == EINVAL);
	CHECK(sun60i_a733_output_get_mode(7, NULL, &mode) == EINVAL);
	CHECK(sun60i_a733_output_get_mode(8, &f, &mode) == ENXIO);
	CHECK(sun60i_a733_output_get_mode(7, &f, &mode) == 0);
	CHECK(sun60i_a733_output_get_mode(7, &f, &mode) == 0);
	CHECK(f.calls[0] == 1);
	CHECK(sun60i_a733_output_get_mode(7, &mode, &changed) == EBUSY);
	changed = mode; changed.pixel_clock_hz++;
	CHECK(sun60i_a733_output_prepare(7, &f, &changed) == EINVAL);
	CHECK(f.calls[1] == 0);
	CHECK(sun60i_a733_output_prepare(7, &f, &mode) == 0);
	CHECK(sun60i_a733_output_prepare(7, &f, &mode) == EALREADY);
	CHECK(sun60i_a733_output_start(7, &f) == 0);
	CHECK(sun60i_a733_output_start(7, &f) == EALREADY);
	f.no_vblank = true;
	CHECK(sun60i_a733_output_observed_vblank(7, &f, &sequence) == ENOENT);
	CHECK(sequence == 123 && f.calls[4] == 0);
	f.no_vblank = false;
	CHECK(sun60i_a733_output_observed_vblank(7, &f, &sequence) == 0);
	CHECK(sequence == 99);
	CHECK(sun60i_a733_output_fault(7, &f, ETIMEDOUT) == 0);
	CHECK(sun60i_a733_output_fault(7, &f, EIO) == ETIMEDOUT);
	CHECK(sun60i_a733_output_start(7, &f) == ETIMEDOUT);
	CHECK(f.calls[4] == 1);

	for (unsigned step = 1; step <= 4; step++) {
		provider_reset();
		memset(&f, 0, sizeof(f)); f.owner = &f; f.fail_step = step;
		CHECK(sun60i_a733_output_register(7, &provider_ops, &f) == 0);
		CHECK(sun60i_a733_output_get_mode(7, &f, &mode) == (step == 1 ? EIO : 0));
		if (step > 1)
			CHECK(sun60i_a733_output_prepare(7, &f, &mode) == (step == 2 ? EIO : 0));
		if (step > 2)
			CHECK(sun60i_a733_output_start(7, &f) == (step == 3 ? EIO : 0));
		sequence = 123;
		CHECK(sun60i_a733_output_observed_vblank(7, &f, &sequence) == EIO);
		CHECK(sequence == 123 && f.calls[4] == 1);
		CHECK(sun60i_a733_output_start(7, &mode) == EBUSY);
		CHECK(sun60i_a733_output_get_mode(7, &f, &mode) == EIO);
		CHECK(f.calls[step - 1] == 1);
	}
	provider_reset();
	memset(&f, 0, sizeof(f)); f.owner = &f; f.invalid_mode = true;
	CHECK(sun60i_a733_output_register(7, &provider_ops, &f) == 0);
	mode = hd;
	CHECK(sun60i_a733_output_get_mode(7, &f, &mode) == EINVAL);
	CHECK(mode.pixel_clock_hz == hd.pixel_clock_hz);
	CHECK(f.calls[4] == 1);
}

static unsigned cache_calls;

static void
provider_trace(const void *arg, struct sun60i_a733_output_trace *out)
{
	CHECK(arg == output_arg && mutex_owned(&output_lock));
	cache_calls++;
	out->version = 1;
	out->valid = 3;
	out->phase = A733_OUTPUT_PREPARE_DONE;
	out->done = 1;
}

static void
cached_trace(void)
{
	struct sun60i_a733_output_ops ops = provider_ops;
	struct provider_fixture f = { .owner = &f };
	struct sun60i_a733_output_trace trace, saved, expected = { 0 };
	struct sun60i_a733_output_mode mode;
	unsigned calls[5];

	CHECK(sizeof(trace) == 13 * sizeof(uint32_t));
	provider_reset();
	memset(&trace, 0xa5, sizeof(trace));
	saved = trace;
	CHECK(sun60i_a733_output_cached_trace(7, NULL) == EINVAL);
	CHECK(sun60i_a733_output_cached_trace(7, &trace) == ENXIO);
	CHECK(sun60i_a733_output_register(7, &ops, &f) == 0);
	CHECK(sun60i_a733_output_cached_trace(7, &trace) == EOPNOTSUPP);
	CHECK(memcmp(&saved, &trace, sizeof(trace)) == 0);
	provider_reset();
	ops.cached_trace = provider_trace;
	CHECK(sun60i_a733_output_register(7, &ops, &f) == 0);
	CHECK(sun60i_a733_output_cached_trace(8, &trace) == ENXIO);
	CHECK(cache_calls == 0);
	CHECK(sun60i_a733_output_get_mode(7, &f, &mode) == 0);
	CHECK(sun60i_a733_output_fault(7, &f, EIO) == 0);
	memcpy(calls, f.calls, sizeof(calls));
	expected.version = 1;
	expected.valid = 3;
	expected.phase = A733_OUTPUT_PREPARE_DONE;
	expected.done = 1;
	for (unsigned i = 0; i < 4; i++) {
		CHECK(sun60i_a733_output_cached_trace(7, &trace) == 0);
		CHECK(memcmp(&trace, &expected, sizeof(trace)) == 0);
		CHECK(memcmp(calls, f.calls, sizeof(calls)) == 0);
	}
	CHECK(cache_calls == 4);
}

static void
provider_phy_trace(const void *arg, struct sun60i_a733_hdmi_phy_trace *out)
{
	CHECK(arg == output_arg && mutex_owned(&output_lock));
	out->version = 1;
	out->valid = 15;
	out->address = 0x0e;
	out->requested = 0x8160;
	out->observed = 0x0160;
}

static void
cached_phy_trace(void)
{
	struct sun60i_a733_output_ops ops = provider_ops;
	struct provider_fixture f = { .owner = &f };
	struct sun60i_a733_hdmi_phy_trace trace, saved, expected = { 0 };
	struct sun60i_a733_output_mode mode;
	unsigned calls[5];

	provider_reset();
	memset(&trace, 0xa5, sizeof(trace));
	saved = trace;
	CHECK(sun60i_a733_output_cached_phy_trace(7, NULL) == EINVAL);
	CHECK(sun60i_a733_output_cached_phy_trace(7, &trace) == ENXIO);
	CHECK(sun60i_a733_output_register(7, &ops, &f) == 0);
	CHECK(sun60i_a733_output_cached_phy_trace(7, &trace) == EOPNOTSUPP);
	CHECK(memcmp(&saved, &trace, sizeof(trace)) == 0);
	provider_reset();
	ops.cached_phy_trace = provider_phy_trace;
	CHECK(sun60i_a733_output_register(7, &ops, &f) == 0);
	CHECK(sun60i_a733_output_cached_phy_trace(8, &trace) == ENXIO);
	CHECK(sun60i_a733_output_get_mode(7, &f, &mode) == 0);
	CHECK(sun60i_a733_output_fault(7, &f, EIO) == 0);
	memcpy(calls, f.calls, sizeof(calls));
	expected.version = 1;
	expected.valid = 15;
	expected.address = 0x0e;
	expected.requested = 0x8160;
	expected.observed = 0x0160;
	for (unsigned i = 0; i < 3; i++) {
		CHECK(sun60i_a733_output_cached_phy_trace(7, &trace) == 0);
		CHECK(memcmp(&trace, &expected, sizeof(trace)) == 0);
		CHECK(memcmp(calls, f.calls, sizeof(calls)) == 0);
	}
}

int
main(void)
{

	timing();
	vblank();
	provider();
	cached_trace();
	cached_phy_trace();
	printf("%u A733 output timing/W0C/provider checks passed\n", checks);
	return 0;
}
