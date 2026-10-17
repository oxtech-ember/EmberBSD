/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD RTL8822C PHY and channel contract, AI-assisted. */
/*
 * The native PHY setup and channel switch must reproduce the reference
 * register trace in rtw8822c-phy.trace exactly, and fail closed.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dev/ic/rtw88var.h>
#include <dev/ic/rtw8822c.h>
#include "rtw8822c-mac-model.h"

static unsigned checks, failures;

/* Isolate the historical PHY trace from measured calibration here only.
 * rtw88-start-contract links the real core, PHY and calibrator together. */
int
rtw8822c_dack(struct rtw88_softc *sc, struct rtw8822c_dack_result *out)
{

	(void)sc;
	memset(out, 0, sizeof(*out));
	out->valid_paths = 3;
	return 0;
}
#define CHECK(x)                                                       \
	do {                                                           \
		checks++;                                              \
		if (!(x)) {                                            \
			failures++;                                    \
			printf("%s:%d: %s\n", __func__, __LINE__, #x); \
		}                                                      \
	} while (0)

static struct {
	uint32_t fail_read;
	unsigned writes_after_fail;
	int failed;
} f;

static int
rd(uint32_t a, unsigned n, uint32_t *v)
{
	if (f.fail_read != 0 && a == f.fail_read) {
		f.failed = 1;
		return EIO;
	}
	*v = model_read(a, n);
	return 0;
}

static int
rd1(void *c, uint32_t a, uint8_t *o)
{
	uint32_t v;
	int e;

	(void)c;
	if ((e = rd(a, 1, &v)) == 0)
		*o = (uint8_t)v;
	return e;
}

static int
rd2(void *c, uint32_t a, uint16_t *o)
{
	uint32_t v;
	int e;

	(void)c;
	if ((e = rd(a, 2, &v)) == 0)
		*o = (uint16_t)v;
	return e;
}

static int rd4(void *c, uint32_t a, uint32_t *o) { (void)c; return rd(a, 4, o); }

static int
wr(uint32_t a, unsigned n, uint32_t v)
{
	if (f.failed)
		f.writes_after_fail++;
	model_write(a, n, v);
	return 0;
}

static int wr1(void *c, uint32_t a, uint8_t v) { (void)c; return wr(a, 1, v); }
static int wr2(void *c, uint32_t a, uint16_t v) { (void)c; return wr(a, 2, v); }
static int wr4(void *c, uint32_t a, uint32_t v) { (void)c; return wr(a, 4, v); }
static void dly(void *c, unsigned us) { (void)c; (void)us; }

static const struct rtw88_bus_ops ops = {
	.read_1 = rd1, .read_2 = rd2, .read_4 = rd4,
	.write_1 = wr1, .write_2 = wr2, .write_4 = wr4,
	.delay_us = dly,
};

static unsigned
load_section(const char *path, const char *name, char out[][32], unsigned max)
{
	FILE *fp = fopen(path, "r");
	char line[128], sec[64];
	unsigned n = 0, count;
	int on = 0;

	if (fp == NULL)
		return 0;
	while (fgets(line, sizeof(line), fp) != NULL) {
		line[strcspn(line, "\n")] = '\0';
		if (sscanf(line, "section %63s writes %u", sec, &count) == 2) {
			on = strcmp(sec, name) == 0;
			continue;
		}
		if (on && line[0] == 'w' && n < max) {
			size_t length = strlen(line);

			if (length >= sizeof(out[0])) {
				fprintf(stderr, "trace entry too long\n");
				fclose(fp);
				return 0;
			}
			memcpy(out[n++], line, length + 1);
		}
	}
	fclose(fp);
	return n;
}

/* The trace's physical EFUSE: (i * 29 + 7) & 0x7f, or blank 0xff. */
static void
efuse_fill(struct rtw88_softc *sc, int blank)
{
	unsigned i;

	for (i = 0; i < sizeof(sc->sc_efuse_phys); i++)
		sc->sc_efuse_phys[i] = blank ? 0xff :
		    (uint8_t)((i * 29 + 7) & 0x7f);
}

static void
setup(struct rtw88_softc *sc)
{
	memset(&f, 0, sizeof(f));
	memset(sc, 0, sizeof(*sc));
	sc->sc_ops = &ops;
	sc->sc_bus = sc;
	sc->sc_xtal = 0x3c;	/* The trace's crystal_cap. */
	efuse_fill(sc, 0);
	model_reset();
}

static void
same_trace(const char *path, const char *name)
{
	static char want[MODEL_TRACE_MAX][32];
	unsigned n, i, diffs = 0;

	n = load_section(path, name, want, MODEL_TRACE_MAX);
	CHECK(n > 0);
	CHECK(model_ntrace == n);
	for (i = 0; i < n && i < model_ntrace && diffs < 5; i++) {
		if (strcmp(want[i], model_trace[i]) != 0) {
			printf("%s write %u: want '%s' got '%s'\n", name, i,
			    want[i], model_trace[i]);
			diffs++;
		}
	}
	CHECK(diffs == 0);
}

static void
test_phy(const char *path)
{
	struct rtw88_softc sc;

	setup(&sc);
	CHECK(rtw8822c_phy_pre(&sc) == 0);
	same_trace(path, "phy-pre");
	model_ntrace = 0;
	CHECK(rtw8822c_phy_post(&sc) == 0);
	same_trace(path, "phy-post");
	/* Unprogrammed trim bytes: the reference skips those writes. */
	setup(&sc);
	efuse_fill(&sc, 1);
	CHECK(rtw8822c_phy_pre(&sc) == 0);
	model_ntrace = 0;
	CHECK(rtw8822c_phy_post(&sc) == 0);
	same_trace(path, "phy-post-blank");
}

static void
test_channels(const char *path)
{
	static const unsigned channels[] = { 1, 6, 11, 13, 14, 36, 100, 149 };
	struct rtw88_softc sc;
	char name[32];
	unsigned i;

	for (i = 0; i < sizeof(channels) / sizeof(channels[0]); i++) {
		setup(&sc);
		CHECK(rtw8822c_set_channel(&sc, channels[i]) == 0);
		snprintf(name, sizeof(name), "channel-%u", channels[i]);
		same_trace(path, name);
	}
}

static void
test_bad_channels(void)
{
	static const unsigned bad[] = { 0, 15, 35, 50, 65, 99, 145, 178, 255 };
	struct rtw88_softc sc;
	unsigned i;

	for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		setup(&sc);
		CHECK(rtw8822c_set_channel(&sc, bad[i]) == EINVAL);
		CHECK(model_ntrace == 0);
	}
}

static void
test_read_failure(void)
{
	struct rtw88_softc sc;

	setup(&sc);
	f.fail_read = 0x3c60;	/* RF 0x18 on path A, read before the switch. */
	CHECK(rtw8822c_set_channel(&sc, 6) == EIO);
	CHECK(f.writes_after_fail == 0);
	setup(&sc);
	f.fail_read = 0x1d70;	/* RXIGI inside the TRX mode setup. */
	CHECK(rtw8822c_phy_post(&sc) == EIO);
	CHECK(f.writes_after_fail == 0);
}

int
main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: %s rtw8822c-phy.trace\n", argv[0]);
		return 2;
	}
	test_phy(argv[1]);
	test_channels(argv[1]);
	test_bad_channels();
	test_read_failure();
	printf("rtw8822c PHY contract: %u assertions, %u failures\n", checks,
	    failures);
	return failures != 0;
}
