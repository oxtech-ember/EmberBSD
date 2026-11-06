/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: actual RTL8822C Wi-Fi-only coex consumer contract, AI-assisted. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dev/ic/rtw88var.h>
#include "rtw8822c-coex-model.h"
#include <dev/ic/rtw8821c_coex.h>
static unsigned assertions;
#define CHECK(x) do { assertions++; if (!(x)) { \
	fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #x); exit(1); \
} } while (0)
static int
read_reg(uint32_t a, unsigned n, uint32_t *v)
{
	if (operation())
		return EIO;
	*v = model_read(a, n);
	return 0;
}
static int rd1(void *c, uint32_t a, uint8_t *v) { uint32_t x; int e;
	(void)c; e = read_reg(a, 1, &x); if (!e) *v = (uint8_t)x; return e; }
static int rd2(void *c, uint32_t a, uint16_t *v) { uint32_t x; int e;
	(void)c; e = read_reg(a, 2, &x); if (!e) *v = (uint16_t)x; return e; }
static int rd4(void *c, uint32_t a, uint32_t *v) {
	(void)c; return read_reg(a, 4, v); }
static int
write_reg(uint32_t a, unsigned n, uint32_t v)
{
	if (operation())
		return EIO;
	model_write(a, n, v);
	return 0;
}
static int wr1(void *c, uint32_t a, uint8_t v) { (void)c; return write_reg(a,1,v); }
static int wr2(void *c, uint32_t a, uint16_t v) { (void)c; return write_reg(a,2,v); }
static int wr4(void *c, uint32_t a, uint32_t v) { (void)c; return write_reg(a,4,v); }
static void delay(void *c, unsigned us) { (void)c; model_delay(us); }
static const struct rtw88_bus_ops ops = {
	.read_1=rd1, .read_2=rd2, .read_4=rd4,
	.write_1=wr1, .write_2=wr2, .write_4=wr4, .delay_us=delay
};
/* Model the separately tested shared WCPU LTE transport. */
int
rtw88_wcpu_lte_update(struct rtw88_softc *sc, uint32_t gen, uint16_t reg,
    uint32_t mask, uint32_t value)
{
	uint32_t ready, old;
	int error;
	(void)gen;
	error = sc->sc_ops->read_4(sc->sc_bus, 0x1700, &ready);
	if (error != 0) return error;
	if ((ready & 0x20000000U) == 0) return ETIMEDOUT;
	error = sc->sc_ops->write_4(sc->sc_bus, 0x1700, 0x800f0000U | reg);
	if (error != 0) return error;
	error = sc->sc_ops->read_4(sc->sc_bus, 0x1708, &old);
	if (error != 0) return error;
	error = sc->sc_ops->read_4(sc->sc_bus, 0x1700, &ready);
	if (error != 0) return error;
	if ((ready & 0x20000000U) == 0) return ETIMEDOUT;
	error = sc->sc_ops->write_4(sc->sc_bus, 0x1704, (old & ~mask) | (value & mask));
	if (error != 0) return error;
	return sc->sc_ops->write_4(sc->sc_bus, 0x1700, 0xc00f0000U | reg);
}

static void
compare(const char *path, unsigned rfe)
{
	struct rtw88_softc sc = { .sc_ops = &ops, .sc_rfe = rfe };
	char line[128];
	FILE *fp = fopen(path, "r");
	unsigned r, count, n = 0;
	int active = 0;
	CHECK(fp != NULL);
	model_reset();
	CHECK(rtw8821c_coex_init(&sc) == 0);
	while (fgets(line, sizeof(line), fp) != NULL) {
		line[strcspn(line, "\n")] = 0;
		if (sscanf(line, "rfe %u events %u", &r, &count) == 2) {
			active = r == rfe;
			if (active) CHECK(count == ntrace);
			continue;
		}
		if (!active || (line[0] != 'r' && line[0] != 'w' && line[0] != 'd')) continue;
		CHECK(n < ntrace);
		if (strcmp(line, trace[n]) != 0) {
			fprintf(stderr, "rfe%u event%u want:%s got:%s\n", rfe, n, line, trace[n]);
			exit(1);
		}
		n++;
	}
	fclose(fp);
	CHECK(n > 10 && n == ntrace);
}
int
main(int argc, char **argv)
{
	static const unsigned profiles[] = {0, 2, 4, 6};
	unsigned i;
	CHECK(argc == 2);
	for (i = 0; i < sizeof(profiles) / sizeof(profiles[0]); i++) compare(argv[1], profiles[i]);
	printf("%u RTL8821C donor trace assertions passed\n", assertions);
	return 0;
}
