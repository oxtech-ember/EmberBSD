/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: actual RTL8822C Wi-Fi-only coex consumer contract, AI-assisted. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dev/ic/rtw88var.h>
#include "rtw8822c-coex-model.h"
/* The integrator adds these declarations to the chip header. */
int rtw8822c_coex_power_on(struct rtw88_softc *);
int rtw8822c_coex_init(struct rtw88_softc *);
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
static int (*const stages[])(struct rtw88_softc *) = {
	rtw8822c_coex_power_on, rtw8822c_coex_init
};
static void
compare(const char *path, unsigned stage, unsigned rfe)
{
	struct rtw88_softc sc = { .sc_ops = &ops, .sc_rfe = (uint8_t)rfe };
	char line[128];
	unsigned s, r, count, n = 0;
	int active = 0;
	FILE *fp = fopen(path, "r");
	CHECK(fp != NULL);
	model_reset();
	CHECK(stages[stage](&sc) == 0);
	while (fgets(line, sizeof(line), fp)) {
		line[strcspn(line, "\n")] = 0;
		if (sscanf(line, "stage %u rfe %u events %u", &s, &r, &count) == 3) {
			active = s == stage && r == rfe;
			if (active)
				CHECK(count == ntrace);
			continue;
		}
		if (!active || (line[0]!='r' && line[0]!='w' && line[0]!='d'))
			continue;
		CHECK(n < ntrace);
		if (strcmp(line, trace[n]) != 0) {
			fprintf(stderr,"stage%u rfe%u event%u want:%s got:%s\n",
			    stage,rfe,n,line,trace[n]);
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
	struct rtw88_softc sc = { .sc_ops = &ops, .sc_rfe = 5 };
	unsigned s,r,i;
	CHECK(argc == 2);
	for (s=0;s<2;s++) {
		unsigned total,ready_count;

		for(r=0;r<7;r++)
			compare(argv[1],s,r);
		model_reset();
		CHECK(stages[s](&sc) == 0);
		total=operations;
		ready_count=ready_reads;
		for(i=1;i<=total;i++) {
			model_reset(); fail_at=i;
			CHECK(stages[s](&sc) == EIO);
			CHECK(failed && after_error == 0 && operations == i);
		}
		model_reset(); stuck=1;
		CHECK(stages[s](&sc) == ETIMEDOUT);
		CHECK(delay_total == 10000 && ntrace < 2100);
		/* Every indirect read and write readiness wait can independently stall. */
		for(i=1;i<=ready_count;i++) {
			model_reset(); fail_ready_at=i;
			CHECK(stages[s](&sc) == ETIMEDOUT);
			CHECK(delay_total >= 10000 && delay_total <= 10001);
			CHECK(ready_reads == i+999);
		}
		/* The last permitted poll must still succeed; the next is forbidden. */
		model_reset(); ready_after=1000;
		CHECK(stages[s](&sc) == 0);
		CHECK(delay_total >= 9990 && delay_total <= 9991);
		model_reset(); ready_after=1001;
		CHECK(stages[s](&sc) == ETIMEDOUT);
		CHECK(delay_total == 10000);
		model_reset(); sc.sc_rfe=7;
		CHECK(stages[s](&sc) == ENOTSUP);
		CHECK(operations == 0);
		sc.sc_rfe=5;
		CHECK(stages[s](NULL) == EINVAL);
	}
	printf("%u RTL8822C coex assertions passed\n",assertions);
	return 0;
}
