/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/types.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <dev/ic/rtw88var.h>
#include <dev/ic/rtw8821c_boot.h>

static uint8_t physical[512];
static uint32_t cfg, ctrl;
static unsigned calls, fail_at, close_fail, closed, pending, timeout, enable_fail, access_state;
static struct rtw88_softc *active;
static int bad;
#define CHECK(x) do { if (!(x)) { printf("line %d: %s\n", __LINE__, #x); bad++; } } while (0)
static int fail(void) { return ++calls == fail_at ? EIO : 0; }
static int rd4(void *p, uint32_t reg, uint32_t *value) {
	(void)p; if (fail()) return EIO;
	if (reg == 0xf0) *value = cfg;
	else if (reg == 0x30) *value = (pending && !timeout) ? (0x80000000U | physical[(ctrl >> 8) & 0x3ff]) : ctrl;
	else return EINVAL;
	return 0;
}
static int wr4(void *p, uint32_t reg, uint32_t value) {
	(void)p; if (fail()) return EIO; if (reg != 0x30) return EINVAL;
	ctrl = value; pending = 1; return 0;
}
static int wr1(void *p, uint32_t reg, uint8_t value) {
	(void)p; if (reg != 0xcf) return EINVAL;
	if (value == 0) {
		closed++; if (close_fail) return ENOSPC;
		access_state = 0; return fail();
	}
	if (enable_fail == 1) return EIO; /* failure before hardware write */
	access_state = value;
	if (enable_fail == 2) return EIO; /* posted write, then bus error */
	return fail();
}
static void delay(void *p, unsigned us) { (void)p; (void)us; }
static const struct rtw88_bus_ops ops = {
	.read_4 = rd4, .write_4 = wr4, .write_1 = wr1, .delay_us = delay
};
int main(void) {
	struct rtw88_softc sc = {0}, before;
	static const uint8_t mac[6] = {2, 1, 2, 3, 4, 5};
	memset(physical, 0xff, sizeof(physical));
	physical[0] = 0x4f; physical[1] = 0x38; memcpy(physical + 2, mac, 6);
	sc.sc_ops = &ops; sc.sc_vendor = 0x10ec; sc.sc_product = 0xc821;
	sc.sc_generation = 7; active = &sc; (void)active; cfg = 0x1000;
	CHECK(rtw8821c_identify(&sc) == 0 && sc.sc_cut == 1);
	calls = 0; CHECK(rtw8821c_read_efuse(&sc) == 0);
	CHECK(closed == 1 && memcmp(sc.sc_mac, mac, 6) == 0);
	CHECK(sc.sc_xtal == 0); /* donor default for unprogrammed 0xff */
	before = sc; calls = 0; fail_at = 3; closed = 0;
	CHECK(rtw8821c_read_efuse(&sc) == EIO && closed == 1);
	CHECK(memcmp(sc.sc_mac, before.sc_mac, 6) == 0);
	CHECK(memcmp(sc.sc_efuse_phys, before.sc_efuse_phys, 512) == 0);
	calls = 0; fail_at = 0; close_fail = 1; closed = 0;
	CHECK(rtw8821c_read_efuse(&sc) == ENOSPC && closed == 1);
	CHECK(memcmp(sc.sc_mac, before.sc_mac, 6) == 0);
	close_fail = 0;
	for (enable_fail = 1; enable_fail <= 2; enable_fail++) {
		calls = 0; closed = 0; access_state = 0;
		CHECK(rtw8821c_read_efuse(&sc) == EIO);
		CHECK(closed == 1 && access_state == 0);
		CHECK(memcmp(sc.sc_mac, before.sc_mac, 6) == 0);
	}
	enable_fail = 2; close_fail = 1; closed = 0;
	CHECK(rtw8821c_read_efuse(&sc) == EIO && closed == 1);
	enable_fail = 0; close_fail = 0; calls = 0; closed = 0; timeout = 1;
	CHECK(rtw8821c_read_efuse(&sc) == ETIMEDOUT && closed == 1);
	CHECK(memcmp(sc.sc_mac, before.sc_mac, 6) == 0);
	timeout = 0; sc.sc_stop_requested = 1; calls = 0;
	CHECK(rtw8821c_read_efuse(&sc) == EINTR && calls == 0);
	sc.sc_stop_requested = 0; cfg = 0x08000000; calls = 0;
	CHECK(rtw8821c_identify(&sc) == ENOTSUP && sc.sc_sys_cfg1 == before.sc_sys_cfg1 && sc.sc_cut == before.sc_cut);
	CHECK(calls == 1); /* one read, no writes */
	cfg = 0x00800000; calls = 0;
	CHECK(rtw8821c_identify(&sc) == ENOTSUP && sc.sc_sys_cfg1 == before.sc_sys_cfg1);
	printf("RTL8821C boot contract: %s\n", bad ? "FAIL" : "PASS");
	return bad != 0;
}
