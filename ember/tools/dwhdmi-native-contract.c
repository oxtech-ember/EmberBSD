/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted; checked I/O and register-sequence oracles. */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); exit(1); } checks++; } while (0)
#define __BIT(n) (UINT32_C(1) << (n))
#define __BITS(hi, lo) ((UINT32_MAX >> (31 - (hi))) & (UINT32_MAX << (lo)))
#define __SHIFTIN(v, mask) ((v) << __builtin_ctz(mask))
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#define DWHDMI_USE_INTERNAL_PHY 1
#define DRM_MODE_FLAG_PHSYNC 1
#define DRM_MODE_FLAG_NHSYNC 2
#define DRM_MODE_FLAG_PVSYNC 4
#define DRM_MODE_FLAG_NVSYNC 8
#define DRM_MODE_FLAG_INTERLACE 16
#define I2C_OP_READ_WITH_STOP 3
#define DDC_ADDR 0x50
#define DDC_SEGMENT_ADDR 0x30

typedef unsigned u_int;
typedef unsigned long bus_size_t;
typedef unsigned i2c_op_t;
typedef unsigned i2c_addr_t;
struct drm_display_mode {
	int clock, crtc_clock;
	int hdisplay, hsync_start, hsync_end, htotal;
	int vdisplay, vsync_start, vsync_end, vtotal;
	int crtc_hdisplay, crtc_hsync_start, crtc_hsync_end, crtc_htotal;
	int crtc_vdisplay, crtc_vsync_start, crtc_vsync_end, crtc_vtotal;
	unsigned flags;
};
struct i2c_controller {
	void *ic_cookie;
	int (*ic_exec)(void *, i2c_op_t, i2c_addr_t, const void *, size_t,
	    void *, size_t, int);
};
struct dwhdmi_connector { bool hdmi_monitor, monitor_audio; };
struct dwhdmi_softc {
	int sc_bst, sc_bsh, sc_dev;
	u_int sc_phytype;
	uint8_t sc_swvol;
	struct i2c_controller *sc_ic, sc_ic_builtin;
	u_int sc_reg_width, sc_flags, sc_version, sc_scl_hcnt, sc_scl_lcnt;
	int (*sc_read)(struct dwhdmi_softc *, bus_size_t, uint8_t *);
	int (*sc_write)(struct dwhdmi_softc *, bus_size_t, uint8_t);
	int sc_io_error;
	bool sc_native_initialized, sc_native_prepared, sc_native_started;
	struct drm_display_mode sc_curmode;
	struct dwhdmi_connector sc_connector;
};
static uint32_t registers[0x20000];
static unsigned calls, fail_at, delay_calls, legacy_calls;
static unsigned prints, tag_inits, writes;
static bool ddc_timeout, drop_clock_write;
static void iic_tag_init(struct i2c_controller *ic) { (void)ic; tag_inits++; }
static int count_print(const char *fmt, ...) { (void)fmt; prints++; return 0; }
#define aprint_error_dev(dev, ...) count_print(__VA_ARGS__)
#define aprint_normal_dev(dev, ...) count_print(__VA_ARGS__)
static uint8_t dwhdmi_read(struct dwhdmi_softc *, bus_size_t);
static void dwhdmi_write(struct dwhdmi_softc *, bus_size_t, uint8_t);
static void delay(unsigned n) { (void)n; delay_calls++; }
static uint8_t drm_match_cea_mode(const struct drm_display_mode *m) { (void)m; return 0; }
static uint8_t bus_space_read_1(int t, int h, bus_size_t r) { (void)t; (void)h; legacy_calls++; return registers[r]; }
static uint32_t bus_space_read_4(int t, int h, bus_size_t r) { (void)t; (void)h; legacy_calls++; return registers[r]; }
static void bus_space_write_1(int t, int h, bus_size_t r, uint8_t v) { (void)t; (void)h; legacy_calls++; registers[r] = v; }
static void bus_space_write_4(int t, int h, bus_size_t r, uint32_t v) { (void)t; (void)h; legacy_calls++; registers[r] = v; }
#define printf count_print
#include "production.c"
#undef printf

static int
checked_read(struct dwhdmi_softc *sc, bus_size_t reg, uint8_t *out)
{
	CHECK(sc->sc_io_error == 0);
	calls++;
	*out = 0xee;
	if (calls == fail_at)
		return EFAULT;
	CHECK(reg < __arraycount(registers));
	*out = registers[reg];
	return 0;
}

static int
checked_write(struct dwhdmi_softc *sc, bus_size_t reg, uint8_t value)
{
	CHECK(sc->sc_io_error == 0);
	calls++;
	if (calls == fail_at)
		return EFAULT;
	CHECK(reg < __arraycount(registers));
	writes++;
	if (!drop_clock_write || reg != HDMI_MC_CLKDIS)
		registers[reg] = value;
	if (reg == HDMI_IH_I2CM_STAT0)
		registers[reg] = 0; /* W1C model. */
	if (reg == HDMI_I2CM_OPERATION && !ddc_timeout) {
		registers[HDMI_IH_I2CM_STAT0] = HDMI_IH_I2CM_STAT0_DONE;
		registers[HDMI_I2CM_DATAI] = registers[HDMI_I2CM_ADDRESS] ^ 0x5a;
	}
	return 0;
}

static struct dwhdmi_softc
fresh(void)
{
	struct dwhdmi_softc sc = { 0 };

	memset(registers, 0, sizeof(registers));
	calls = fail_at = delay_calls = legacy_calls = 0;
	prints = tag_inits = writes = 0;
	ddc_timeout = drop_clock_write = false;
	sc.sc_reg_width = 1;
	sc.sc_read = checked_read;
	sc.sc_write = checked_write;
	return sc;
}

static const struct drm_display_mode mode = {
	108000, 108000,
	1280, 1328, 1440, 1688, 1024, 1025, 1028, 1066,
	1280, 1328, 1440, 1688, 1024, 1025, 1028, 1066,
	DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC
};

static void
io(void)
{
	struct dwhdmi_softc sc;

	for (unsigned width = 1; width <= 4; width += 3) {
		sc = fresh(); sc.sc_read = NULL; sc.sc_write = NULL; sc.sc_reg_width = width;
		registers[7 * width] = 0x123456ab;
		CHECK(dwhdmi_read(&sc, 7) == 0xab);
		dwhdmi_write(&sc, 7, 0x35);
		CHECK(registers[7 * width] == 0x35 && legacy_calls == 2);
	}
	sc = fresh(); fail_at = 1;
	CHECK(dwhdmi_read(&sc, 7) == 0);
	CHECK(sc.sc_io_error == EFAULT && calls == 1);
	dwhdmi_write(&sc, 7, 0x35);
	CHECK(dwhdmi_read(&sc, 7) == 0 && calls == 1 && legacy_calls == 0);
	sc = fresh(); fail_at = 1;
	dwhdmi_write(&sc, 7, 0x35);
	CHECK(sc.sc_io_error == EFAULT && calls == 1);
	dwhdmi_write(&sc, 8, 0x35);
	CHECK(dwhdmi_read(&sc, 7) == 0 && calls == 1);
}

static void
prepare(void)
{
	struct dwhdmi_softc sc = fresh();
	struct drm_display_mode bad = mode;
	unsigned count, prepare_count;

	CHECK(dwhdmi_native_start(&sc) == EINVAL && calls == 0);
	bad.crtc_htotal++;
	CHECK(dwhdmi_native_prepare(&sc, &bad) == EINVAL && calls == 0);
	bad = mode; bad.flags |= 16;
	CHECK(dwhdmi_native_prepare(&sc, &bad) == EINVAL && calls == 0);
	bad = mode; bad.crtc_vtotal = bad.vtotal = 1300;
	CHECK(dwhdmi_native_prepare(&sc, &bad) == EINVAL && calls == 0);
	CHECK(dwhdmi_native_prepare(&sc, &mode) == 0);
	CHECK(sc.sc_native_prepared && !sc.sc_native_started);
	CHECK(registers[HDMI_FC_INHACTIV0] == 0 && registers[HDMI_FC_INHACTIV1] == 5);
	CHECK(registers[HDMI_FC_INVACTIV0] == 0 && registers[HDMI_FC_INVACTIV1] == 4);
	CHECK(registers[HDMI_FC_INVBLANK] == 42);
	CHECK(registers[HDMI_FC_INVIDCONF] == 0x70); /* RGB, positive sync, DVI. */
	CHECK(registers[HDMI_MC_CLKDIS] == 0); /* prepare does not start clocks. */
	prepare_count = count = calls;
	CHECK(dwhdmi_native_prepare(&sc, &mode) == EALREADY && calls == count);
	CHECK(dwhdmi_native_start(&sc) == 0 && sc.sc_native_started);
	CHECK(registers[HDMI_MC_CLKDIS] == 0x7c); /* Only pixel and TMDS run. */
	count = calls;
	CHECK(dwhdmi_native_start(&sc) == EALREADY && calls == count);
	for (unsigned failure = 1; failure <= count; failure++) {
		sc = fresh(); fail_at = failure;
		int error = dwhdmi_native_prepare(&sc, &mode);
		if (error == 0)
			error = dwhdmi_native_start(&sc);
		CHECK(error == EFAULT && sc.sc_io_error == EFAULT);
		CHECK(calls == failure && !sc.sc_native_started);
		CHECK(sc.sc_native_prepared == (failure > prepare_count));
		CHECK(dwhdmi_native_start(&sc) == EFAULT && calls == failure);
		CHECK(dwhdmi_native_prepare(&sc, &mode) == EFAULT && calls == failure);
	}
}

static void
stop_restart(void)
{
	struct dwhdmi_softc sc = fresh();
	unsigned count;

	CHECK(dwhdmi_native_stop(NULL) == EINVAL);
	CHECK(dwhdmi_native_stop(&sc) == EINVAL && calls == 0);
	sc.sc_flags = DWHDMI_USE_INTERNAL_PHY;
	CHECK(dwhdmi_native_stop(&sc) == EOPNOTSUPP && calls == 0);
	sc.sc_flags = 0;
	CHECK(dwhdmi_native_prepare(&sc, &mode) == 0);
	count = calls;
	CHECK(dwhdmi_native_stop(&sc) == EALREADY && calls == count);
	for (unsigned cycle = 0; cycle < 3; cycle++) {
		CHECK(dwhdmi_native_start(&sc) == 0);
		CHECK(dwhdmi_native_stop(&sc) == 0);
		CHECK((registers[HDMI_MC_CLKDIS] & 0x7f) == 0x7f);
		CHECK(!sc.sc_native_started && sc.sc_native_prepared);
		CHECK(memcmp(&sc.sc_curmode, &mode, sizeof(mode)) == 0);
		count = calls;
		CHECK(dwhdmi_native_stop(&sc) == EALREADY && calls == count);
	}
	for (unsigned failure = 1; failure <= 3; failure++) {
		sc = fresh();
		CHECK(dwhdmi_native_prepare(&sc, &mode) == 0);
		CHECK(dwhdmi_native_start(&sc) == 0);
		if (failure <= 2)
			fail_at = calls + failure;
		else
			drop_clock_write = true;
		CHECK(dwhdmi_native_stop(&sc) == (failure <= 2 ? EFAULT : EIO));
		CHECK(sc.sc_native_started && sc.sc_native_prepared);
		count = calls;
		CHECK(dwhdmi_native_start(&sc) == sc.sc_io_error && calls == count);
		CHECK(dwhdmi_native_stop(&sc) == sc.sc_io_error && calls == count);
	}
}

static void
ddc(void)
{
	struct dwhdmi_softc sc = fresh();
	uint8_t block = 0, data[2];
	unsigned count;

	CHECK(dwhdmi_ddc_exec(&sc, I2C_OP_READ_WITH_STOP, DDC_ADDR,
	    &block, 1, data, sizeof(data), 0) == 0);
	CHECK(data[0] == 0x5a && data[1] == 0x5b && delay_calls == 0);
	count = calls;
	for (unsigned failure = 1; failure <= count; failure++) {
		sc = fresh(); fail_at = failure;
		CHECK(dwhdmi_ddc_exec(&sc, I2C_OP_READ_WITH_STOP, DDC_ADDR,
		    &block, 1, data, sizeof(data), 0) == EFAULT);
		CHECK(calls == failure && delay_calls == 0);
	}
}

static void
ddc_counts(void)
{
	struct dwhdmi_softc sc = fresh();
	uint8_t block = 0, data = 0;

	sc.sc_scl_hcnt = 0x1234;
	sc.sc_scl_lcnt = 0xabcd;
	registers[0x730c] = 0xa5;
	registers[0x730e] = 0x5a;
	CHECK(dwhdmi_ddc_exec(&sc, I2C_OP_READ_WITH_STOP, DDC_ADDR,
	    &block, 1, &data, sizeof(data), 0) == 0);
	/* These numeric addresses are independent of the production macros. */
	CHECK(registers[0x7e0b] == 0x12 && registers[0x7e0c] == 0x34);
	CHECK(registers[0x7e0d] == 0xab && registers[0x7e0e] == 0xcd);
	CHECK(registers[0x730c] == 0xa5 && registers[0x730e] == 0x5a);

	sc = fresh();
	sc.sc_scl_hcnt = 0xd8;
	sc.sc_scl_lcnt = 0xfe;
	registers[0x7e0b] = registers[0x7e0d] = 0x55;
	CHECK(dwhdmi_ddc_exec(&sc, I2C_OP_READ_WITH_STOP, DDC_ADDR,
	    &block, 1, &data, sizeof(data), 0) == 0);
	CHECK(registers[0x7e0b] == 0 && registers[0x7e0c] == 0xd8);
	CHECK(registers[0x7e0d] == 0 && registers[0x7e0e] == 0xfe);

	sc = fresh();
	sc.sc_scl_hcnt = 65536;
	CHECK(dwhdmi_ddc_exec(&sc, I2C_OP_READ_WITH_STOP, DDC_ADDR,
	    &block, 1, &data, sizeof(data), 0) == ERANGE && calls == 0);
	sc = fresh();
	sc.sc_scl_lcnt = 65536;
	CHECK(dwhdmi_ddc_exec(&sc, I2C_OP_READ_WITH_STOP, DDC_ADDR,
	    &block, 1, &data, sizeof(data), 0) == ERANGE && calls == 0);
}

static void
initialization(void)
{
	struct dwhdmi_softc sc = fresh();
	uint8_t block = 0, data = 0;
	unsigned count;

	registers[0] = 0x21; registers[1] = 0x34; registers[6] = 0xf3;
	CHECK(dwhdmi_native_init(&sc) == 0);
	CHECK(sc.sc_native_initialized && sc.sc_version == 0x2134);
	CHECK(sc.sc_phytype == 0xf3 && calls == 3 && writes == 0);
	CHECK(sc.sc_ic == &sc.sc_ic_builtin && sc.sc_ic->ic_cookie == &sc);
	CHECK(sc.sc_ic->ic_exec == dwhdmi_ddc_exec && tag_inits == 1);
	CHECK(prints == 0 && !sc.sc_native_prepared && !sc.sc_native_started);
	CHECK(dwhdmi_native_init(&sc) == EALREADY && tag_inits == 1 && calls == 3);
	for (unsigned failure = 1; failure <= 3; failure++) {
		sc = fresh(); fail_at = failure;
		CHECK(dwhdmi_native_init(&sc) == EFAULT && calls == failure);
		CHECK(!sc.sc_native_initialized && tag_inits == 0 && writes == 0);
		CHECK(dwhdmi_native_init(&sc) == EFAULT && calls == failure);
		CHECK(prints == 0);
	}
	for (unsigned invalid = 0; invalid < 5; invalid++) {
		sc = fresh();
		if (invalid == 0) sc.sc_read = NULL;
		if (invalid == 1) sc.sc_write = NULL;
		if (invalid == 2) sc.sc_reg_width = 4;
		if (invalid == 3) sc.sc_flags = DWHDMI_USE_INTERNAL_PHY;
		if (invalid == 4) sc.sc_native_prepared = true;
		CHECK(dwhdmi_native_init(&sc) == EINVAL);
		CHECK(calls == 0 && prints == 0 && tag_inits == 0);
	}
	/* The same production initializer keeps legacy identification output. */
	sc = fresh();
	CHECK(dwhdmi_init(&sc, false) == 0 && prints == 1 && tag_inits == 1);
	sc = fresh(); sc.sc_reg_width = 2;
	CHECK(dwhdmi_init(&sc, false) == EINVAL && prints == 1 && calls == 0);

	/* Quiet DDC errors retain their errno and do not add diagnostic I/O. */
	sc = fresh(); CHECK(dwhdmi_native_init(&sc) == 0); count = calls;
	CHECK(dwhdmi_ddc_exec(&sc, 0, DDC_ADDR, &block, 1, &data, 1, 0) == ENXIO);
	CHECK(dwhdmi_ddc_exec(&sc, I2C_OP_READ_WITH_STOP, DDC_ADDR,
	    &block, 1, &data, 257, 0) == ERANGE);
	CHECK(calls == count && prints == 0);
	ddc_timeout = true;
	CHECK(dwhdmi_ddc_exec(&sc, I2C_OP_READ_WITH_STOP, DDC_ADDR,
	    &block, 1, &data, 1, 0) == ETIMEDOUT);
	CHECK(delay_calls == 10000 && prints == 0);
	count = calls;
	sc = fresh(); ddc_timeout = true;
	CHECK(dwhdmi_ddc_exec(&sc, I2C_OP_READ_WITH_STOP, DDC_ADDR,
	    &block, 1, &data, 1, 0) == ETIMEDOUT);
	CHECK(prints == 1 && delay_calls == 10000 && calls == count - 3 + 1);
}

int
main(void)
{
	io(); prepare(); stop_restart(); ddc(); ddc_counts(); initialization();
	printf("%u DW-HDMI checked I/O/native RGB checks passed\n", checks);
	return 0;
}
