#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <dev/ic/rtw88var.h>
#include "rtw8821c_phy.h"
#include "rtw88_hw.h"
static uint8_t regs[0x4000];
static unsigned writes, mode, tracing, events;
static uint64_t hash;
static unsigned fault_at, operations, checks, stale_at, failure_ops;
static uint32_t invalidate_read_addr, invalidate_write_addr;
static int invalidated;
static int generation(void *p)
{
	(void)p;
	checks++;
	if (invalidated)
		return ECANCELED;
	if (stale_at && checks == stale_at) {
		failure_ops = operations;
		return ECANCELED;
	}
	return 0;
}
static int fail_now(void)
{
	operations++;
	return fault_at && operations == fault_at;
}
static void ev(uint64_t a, uint64_t b, uint64_t c)
{
	uint64_t v[3] = {a, b, c};
	unsigned i, j;
	if (!tracing)
		return;
	for (i = 0; i < 3; i++)
		for (j = 0; j < 8; j++) {
			hash ^= (v[i] >> (j * 8)) & 255;
			hash *= UINT64_C(1099511628211);
		}
	events++;
}
static int r1(void *v, uint32_t a, uint8_t *x)
{
	(void)v;
	if (fail_now())
		return EIO;
	if (a >= sizeof(regs))
		return 1;
	*x = regs[a];
	return 0;
}
static int r4(void *v, uint32_t a, uint32_t *x)
{
	(void)v;
	if (fail_now())
		return EIO;
	if (a + 4 > sizeof(regs))
		return 1;
	memcpy(x, regs + a, 4);
	if (invalidate_read_addr && invalidate_read_addr == a)
		invalidated = 1;
	return 0;
}
static int w1(void *v, uint32_t a, uint8_t x)
{
	(void)v;
	if (fail_now())
		return EIO;
	if (a >= sizeof(regs))
		return 1;
	regs[a] = x;
	writes++;
	ev(1, a, x);
	return 0;
}
static int w2(void *v, uint32_t a, uint16_t x)
{
	(void)v;
	if (fail_now())
		return EIO;
	if (a + 2 > sizeof(regs))
		return 1;
	memcpy(regs + a, &x, 2);
	writes++;
	return 0;
}
static int w4(void *v, uint32_t a, uint32_t x)
{
	(void)v;
	if (fail_now())
		return EIO;
	if (a + 4 > sizeof(regs))
		return 1;
	memcpy(regs + a, &x, 4);
	writes++;
	ev(4, a, x);
	if (invalidate_write_addr && invalidate_write_addr == a)
		invalidated = 1;
	if (a == 0xc90) {
		uint32_t direct = 0x2800 + (((x >> 20) & 255) * 4),
			 value = x & 0xfffff;
		memcpy(regs + direct, &value, 4);
	}
	return 0;
}
static void delay(void *v, unsigned u)
{
	(void)v;
	ev(5, u, 0);
}
static int wait_ms(void *p, unsigned ms)
{
	(void)p;
	if (mode == 3)
		return EINTR;
	return ms == 20 ? 0 : EINVAL;
}
static int command(void *p, const uint8_t *pkt, size_t n)
{
	uint32_t v = 0xabcde;
	(void)p;
	if (n != 32 || pkt[0] != 1 || pkt[1] != 0xff || pkt[2] != 0x0e ||
	    pkt[4] != 9 || pkt[8] != 2)
		return EINVAL;
	if (mode == 2)
		return EIO;
	if (mode != 1 && mode != 3)
		memcpy(regs + 0x2820, &v, 4);
	return 0;
}
int main(void)
{
	static const struct rtw88_bus_ops ops = {r1, r4, w1, w2, w4, delay};
	struct rtw88_softc sc = {&ops, 0, 0, 0};
	struct rtw8821c_phy ctx = {0};
	struct rtw88_channel_tuple t = {6, 6, 20};
	uint32_t v;
	unsigned n, total, k;
	ctx.sc = &sc;
	ctx.command = command;
	ctx.wait_ms = wait_ms;
	ctx.associated = 1;
	ctx.post_valid = 1;
	tracing = 1;
	hash = UINT64_C(14695981039346656037);
	if (rtw8821c_set_channel_tuple(&ctx, &t) || events != 37 ||
	    hash != UINT64_C(0xe0cd5cca00c0bf43))
		return 11;
	memset(regs, 0, sizeof(regs));
	ctx.efuse.rfe_option = 2;
	events = 0;
	hash = UINT64_C(14695981039346656037);
	if (rtw8821c_set_channel_tuple(&ctx, &t) || events != 37 ||
	    hash != UINT64_C(0x92608e4adcf9f8c3))
		return 12;
	memset(regs, 0, sizeof(regs));
	ctx.efuse.rfe_option = 0;
	tracing = 0;
	writes = 0;
	/* Both crystal registers must use the normalized EFUSE value. */
	{
		static const uint8_t raw[] = { 0xff, 0x2b, 0x00 };
		static const uint8_t expected[] = { 0, 0x2b, 0 };
		uint32_t xtal, pll;

		for (k = 0; k < sizeof(raw); k++) {
			ctx.efuse.xtal = raw[k];
			if (rtw8821c_phy_post(&ctx))
				return 20;
			memcpy(&xtal, regs + 0x24, sizeof(xtal));
			memcpy(&pll, regs + 0x28, sizeof(pll));
			if (((xtal >> 25) & 0x3f) != expected[k] ||
			    ((pll >> 1) & 0x3f) != expected[k])
				return 21;
		}
	}
	ctx.efuse.xtal = 20;
	ctx.efuse.logical[0xc6] = 0;
	if (rtw8821c_phy_pre(&ctx) || rtw8821c_phy_post(&ctx) ||
	    rtw8821c_set_channel_tuple(&ctx, &t))
		return 1;
	memcpy(&v, regs + 0x2860, 4);
	if ((v & 0x70fff) != 0xc06)
		return 2;
	memcpy(&v, regs + 0xcb8, 4);
	if ((v & 0x00f50000) != 0x00700000)
		return 3;
	n = writes;
	t.primary = 36;
	t.center = 36;
	if (rtw8821c_set_channel_tuple(&ctx, &t) != ENOTSUP || writes != n)
		return 4;
	sc.sc_stop_requested = 1;
	t.primary = 6;
	t.center = 6;
	if (rtw8821c_set_channel_tuple(&ctx, &t) != EINTR || writes != n)
		return 5;
	sc.sc_stop_requested = 0;
	if (rtw8821c_calibrate(&ctx))
		return 6;
	memcpy(&v, regs + 0x2820, 4);
	if (v != 0)
		return 7;
	mode = 1;
	if (rtw8821c_calibrate(&ctx) != ETIMEDOUT)
		return 8;
	mode = 2;
	if (rtw8821c_calibrate(&ctx) != EIO)
		return 9;
	mode = 3;
	if (rtw8821c_calibrate(&ctx) != EINTR)
		return 13;
	mode = 0;
	v = 1;
	memcpy(regs + 0x1bf0, &v, 4);
	if (rtw8821c_calibrate(&ctx) != EIO)
		return 10;
	/* Every bus failure stops the remaining retune transaction. */
	mode = 0;
	v = 0;
	memcpy(regs + 0x1bf0, &v, 4);
	operations = fault_at = 0;
	ctx.post_valid = 1;
	if (rtw8821c_set_channel_tuple(&ctx, &t) != 0)
		return 14;
	total = operations;
	for (k = 1; k <= total; k++) {
		memset(regs, 0, sizeof(regs));
		operations = 0;
		fault_at = k;
		if (rtw8821c_set_channel_tuple(&ctx, &t) != EIO ||
		    operations != k)
			return 15;
	}
	fault_at = 0;
	ctx.check = generation;
	checks = stale_at = operations = 0;
	if (rtw8821c_set_channel_tuple(&ctx, &t) != 0)
		return 18;
	total = checks;
	for (k = 1; k <= total; k++) {
		memset(regs, 0, sizeof(regs));
		checks = operations = 0;
		stale_at = k;
		if (rtw8821c_set_channel_tuple(&ctx, &t) != ECANCELED ||
		    operations != failure_ops)
			return 19;
	}
	/* Generation changes in the final callback must not publish success. */
	stale_at = 0;
	invalidated = 0;
	invalidate_read_addr = 0x2954;
	v = 0x12345678;
	if (rtw8821c_read_rf(&ctx, 0x55, &v) != ECANCELED || v != 0x12345678)
		return 20;
	invalidate_read_addr = 0;
	invalidated = 0;
	invalidate_write_addr = 0x1c94;
	ctx.post_valid = 1;
	if (rtw8821c_phy_post(&ctx) != ECANCELED || ctx.post_valid != 0)
		return 21;
	invalidate_write_addr = 0;
	invalidated = 0;
	invalidate_read_addr = 0x1bf0;
	if (rtw8821c_calibrate(&ctx) != ECANCELED)
		return 22;
	invalidate_read_addr = 0;
	invalidated = 0;
	ctx.check = NULL;
	stale_at = 0;
	memset(regs, 0, sizeof(regs));
	if (rtw8821c_write_rf_mask(&ctx, 0x55, 0xf0, 0xa) ||
	    rtw8821c_read_rf(&ctx, 0x55, &v) || v != 0xa0)
		return 16;
	n = writes;
	if (rtw8821c_write_rf_mask(&ctx, 0x55, 0xf0, 0x10) != EINVAL ||
	    writes != n)
		return 17;
	puts("native IQK, pre/post, 2G channel, invalid channel and cancel: "
	     "PASS");
	return 0;
}
