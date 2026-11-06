/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: PCI write order from FreeBSD a143197 rtw8821c power tables. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <dev/ic/rtw88var.h>
#include <dev/ic/rtw8821c_boot.h>
static uint8_t reg[0x400];
static uint32_t written[128];
static unsigned nwrite, calls, fail_at, cancel_at, after_failure, failed, timeout;
static struct rtw88_softc *active;
static unsigned checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { printf("line %d: %s\n", __LINE__, #x); failures++; } } while (0)
static int op(void) { if (failed) after_failure++; calls++; if (calls == fail_at) { failed = 1; return EIO; } return 0; }
static int rd(void *p, uint32_t a, uint8_t *v) {
	(void)p; if (op()) return EIO; if (a >= sizeof(reg)) return EINVAL;
	if (a == 5 && !timeout) reg[a] &= (uint8_t)~3U;
	*v = reg[a]; if (calls == cancel_at) active->sc_stop_requested = 1; return 0;
}
static int wr(void *p, uint32_t a, uint8_t v) {
	(void)p; if (op()) return EIO; if (a >= sizeof(reg)) return EINVAL;
	reg[a] = v; if (nwrite < 128) written[nwrite++] = (a << 8) | v;
	if (calls == cancel_at) active->sc_stop_requested = 1; return 0;
}
static void delay(void *p, unsigned us) { (void)p; (void)us; }
static const struct rtw88_bus_ops ops = { .read_1=rd, .write_1=wr, .delay_us=delay };
static void reset(struct rtw88_softc *sc) {
	memset(sc, 0, sizeof(*sc)); memset(reg, 0, sizeof(reg));
	sc->sc_ops=&ops; sc->sc_generation=1; active=sc; reg[6]=2;
	nwrite=calls=fail_at=cancel_at=after_failure=failed=timeout=0;
}
int main(void) {
	static const uint32_t on[] = {0x500,0x30000,0x30100,0x500,0x7501,0x7500,0x603,0x500,0x500,0x501,0x2008,0x7420,0x2200,0x62e0,0x6100,0x7c00};
	static const uint32_t off[] = {0x9300,0x1f00,0x4900,0x603,0x200,0x502,0x2000,0x6700,0x504,0x8100,0x9000};
	struct rtw88_softc sc; unsigned count, i;
	reset(&sc); CHECK(rtw8821c_power(&sc, 1) == 0);
	CHECK(nwrite == sizeof(on)/sizeof(on[0])); CHECK(memcmp(written,on,sizeof(on)) == 0);
	count = calls;
	for (i=1; i<=count; i++) { reset(&sc); fail_at=i; CHECK(rtw8821c_power(&sc, 1) == EIO); CHECK(after_failure == 0); }
	for (i=1; i<=count; i++) { reset(&sc); cancel_at=i; CHECK(rtw8821c_power(&sc, 1) == EINTR); }
	reset(&sc); CHECK(rtw8821c_power(&sc, 0) == 0);
	CHECK(nwrite == sizeof(off)/sizeof(off[0])); CHECK(memcmp(written,off,sizeof(off)) == 0);
	count = calls;
	for (i=1; i<=count; i++) { reset(&sc); sc.sc_stop_requested=1; fail_at=i; CHECK(rtw8821c_power(&sc,0) == EIO); CHECK(after_failure == 0); }
	reset(&sc); sc.sc_stop_requested=1; CHECK(rtw8821c_power(&sc,0) == 0);
	reset(&sc); timeout=1; CHECK(rtw8821c_power(&sc,1) == ETIMEDOUT);
	reset(&sc); sc.sc_cut=7; CHECK(rtw8821c_power(&sc,1) == EINVAL && calls == 0);
	printf("RTL8821C PCI power flow: %u checks, %u failures\n", checks, failures);
	return failures != 0;
}
