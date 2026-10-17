/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD RTL8822C MAC init contract, AI-assisted. */
/*
 * The native MAC init must reproduce the reference register trace in
 * rtw8822c-mac.trace exactly, and fail closed on bus and hardware errors.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dev/ic/rtw88var.h>
#include <dev/ic/rtw8822c.h>
#include "rtw8822c-mac-model.h"

static unsigned checks, failures;
#define CHECK(x)                                                       \
	do {                                                           \
		checks++;                                              \
		if (!(x)) {                                            \
			failures++;                                    \
			printf("%s:%d: %s\n", __func__, __LINE__, #x); \
		}                                                      \
	} while (0)

static struct {
	uint32_t fail_read;	/* Register whose read fails, or 0. */
	int llt_stuck;
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

static int
rd4(void *c, uint32_t a, uint32_t *o)
{
	(void)c;
	return rd(a, 4, o);
}

static int
wr(uint32_t a, unsigned n, uint32_t v)
{
	if (f.failed)
		f.writes_after_fail++;
	model_write(a, n, v);
	if (f.llt_stuck && a == 0x0208)
		model_reg[0x0208] |= 0x01;
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

/* Load the "cut N" section of the reference trace. */
static unsigned
load_trace(const char *path, unsigned cut, char out[][32], unsigned max)
{
	FILE *fp = fopen(path, "r");
	char line[128];
	unsigned n = 0, c, want = 0;
	int on = 0;

	if (fp == NULL)
		return 0;
	while (fgets(line, sizeof(line), fp) != NULL) {
		line[strcspn(line, "\n")] = '\0';
		if (sscanf(line, "cut %u writes %u", &c, &want) == 2) {
			on = (c == cut);
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

static void
setup(struct rtw88_softc *sc, unsigned cut)
{
	memset(&f, 0, sizeof(f));
	memset(sc, 0, sizeof(*sc));
	sc->sc_ops = &ops;
	sc->sc_bus = sc;
	sc->sc_cut = (uint8_t)cut;
	model_reset();
}

static void
test_reference_trace(const char *path, unsigned cut)
{
	static char want[MODEL_TRACE_MAX][32];
	struct rtw88_softc sc;
	unsigned n, i, first = 0;

	n = load_trace(path, cut, want, MODEL_TRACE_MAX);
	CHECK(n > 100);
	setup(&sc, cut);
	CHECK(rtw8822c_mac_init(&sc) == 0);
	CHECK(model_ntrace == n);
	for (i = 0; i < n && i < model_ntrace; i++) {
		if (strcmp(want[i], model_trace[i]) != 0) {
			printf("cut %u write %u: want '%s' got '%s'\n", cut, i,
			    want[i], model_trace[i]);
			first++;
			if (first > 5)
				break;
		}
	}
	CHECK(first == 0);
}

static void
test_read_failure(void)
{
	struct rtw88_softc sc;

	setup(&sc, 3);
	f.fail_read = 0x0420;	/* FWHW_TXQ_CTRL, mid-sequence. */
	CHECK(rtw8822c_mac_init(&sc) == EIO);
	CHECK(f.writes_after_fail == 0);
}

static void
test_llt_timeout(void)
{
	struct rtw88_softc sc;

	setup(&sc, 3);
	f.llt_stuck = 1;
	CHECK(rtw8822c_mac_init(&sc) == ETIMEDOUT);
}

static void
test_h2c_queue_mismatch(void)
{
	struct rtw88_softc sc;

	setup(&sc, 3);
	model_reset();
	model_reg[0x10d4] = 0x40;	/* Write pointer ahead of read pointer. */
	CHECK(rtw8822c_mac_init(&sc) == EIO);
}

int
main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: %s rtw8822c-mac.trace\n", argv[0]);
		return 2;
	}
	test_reference_trace(argv[1], 2);
	test_reference_trace(argv[1], 3);
	test_read_failure();
	test_llt_timeout();
	test_h2c_queue_mismatch();
	printf("rtw8822c MAC init contract: %u assertions, %u failures\n",
	    checks, failures);
	return failures != 0;
}
