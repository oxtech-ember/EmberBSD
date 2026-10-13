/* Origin: EmberBSD - exercise bounded native RTL8723DS read transport. */
/* SPDX-License-Identifier: BSD-2-Clause */
#define main existing_fdt_contract
#include "a133-fdt-test.c"
#undef main
#include "sdio-fixture.inc"
#include <errno.h>
#include "rtl8723ds_read.h"

struct command_fixture {
	unsigned int calls, fail_at;
	uint32_t card_error;
	uint32_t first_address;
};

static int
command(void *cookie, uint32_t arg, uint32_t *response)
{
	struct command_fixture *fixture = cookie;
	uint32_t address = (arg >> 9) & 0x1ffff;
	unsigned int function = (arg >> 28) & 7;
	static const uint8_t data[] = { 0x12, 0x34, 0x56, 0x78 };

	/* An accidental write/function/domain change must fail the fixture. */
	assert((arg & 0x88000100) == 0);
	assert(function == (fixture->first_address == 2 ? 0 : 1));
	assert(address == fixture->first_address + fixture->calls);
	fixture->calls++;
	if (fixture->fail_at == fixture->calls)
		return ETIMEDOUT;
	*response = 0x2000 | fixture->card_error |
	    data[(fixture->calls - 1) % 4];
	return 0;
}

static void
read_contract(void)
{
	struct command_fixture fixture = { .first_address = 0x100f0 };
	struct rtl8723ds_read_ops ops = { command, &fixture };
	uint32_t value = 0xdeadbeef;
	uint8_t byte = 0x99;
	static const uint32_t bad[] = {
		0x10250ffd, 0x10251000, 0x1026fffd, 0x10270000,
		0x10340000, 0xffffffff
	};
	static const uint32_t r5[] = { 0x8000, 0x4000, 0x800, 0x200, 0x100 };

	assert(rtl8723ds_read32(&ops, 0x102600f0, &value) == 0);
	assert(value == 0x78563412 && fixture.calls == 4);
	fixture.calls = 0; fixture.first_address = 0x24;
	assert(rtl8723ds_read32(&ops, 0x10250024, &value) == 0);
	assert(value == 0x78563412 && fixture.calls == 4);
	for (unsigned int i = 1; i <= 4; i++) {
		fixture.calls = 0; fixture.first_address = 0x100f0;
		fixture.fail_at = i; value = 0xdeadbeef;
		assert(rtl8723ds_read32(&ops, 0x102600f0, &value) == ETIMEDOUT);
		assert(value == 0xdeadbeef && fixture.calls == i);
	}
	fixture.fail_at = 0;
	for (unsigned int i = 0; i < sizeof(r5) / sizeof(r5[0]); i++) {
		fixture.calls = 0; fixture.card_error = r5[i]; value = 0xdeadbeef;
		assert(rtl8723ds_read32(&ops, 0x102600f0, &value) != 0);
		assert(value == 0xdeadbeef && fixture.calls == 1);
	}
	fixture.calls = 0; fixture.card_error = 0;
	for (unsigned int i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		value = 0xdeadbeef;
		assert(rtl8723ds_read32(&ops, bad[i], &value) == EINVAL);
		assert(value == 0xdeadbeef && fixture.calls == 0);
	}
	assert(rtl8723ds_read32(NULL, 0x102600f0, &value) == EINVAL);
	assert(rtl8723ds_read32(&ops, 0x102600f0, NULL) == EINVAL);
	assert(rtl8723ds_read8(&ops, 2, 0x24, &byte) == EINVAL);
	assert(rtl8723ds_read8(&ops, 1, 0x20000, &byte) == EINVAL);
	assert(byte == 0x99 && fixture.calls == 0);
	fixture.first_address = 2;
	assert(rtl8723ds_read8(&ops, 0, 2, &byte) == 0);
	assert(byte == 0x12 && fixture.calls == 1);
	fixture.calls = 0; fixture.first_address = 0x1fffc;
	assert(rtl8723ds_read32(&ops, 0x1026fffc, &value) == 0);
	assert(value == 0x78563412 && fixture.calls == 4);
	puts("RTL8723DS CMD52: address domains, byte order, R5/host errors and atomic output passed");
}

static void
match_contract(void)
{
	unsigned char fdt[16384];
	int host, node;

	for (unsigned int which = 0; which < 12; which++) {
		sdio_fixture(fdt, "allwinner,a133");
		assert(sun50i_a133_fdt_fixup(fdt) == 0);
		assert(fdt_setprop(fdt, 0, "ember,ys-m33-rtl8723ds-read-probe", NULL, 0) == 0);
		host = fdt_path_offset(fdt, HOST);
		if (which == 0) {
			check_host(fdt);
			assert(rtl8723ds_read_match(fdt, host, 0x024c, 0xd723, 1, 7));
			continue;
		}
		if (which == 1) assert(fdt_delprop(fdt, 0, "ember,ys-m33-rtl8723ds-read-probe") == 0);
		if (which == 2) assert(fdt_setprop_u32(fdt, 0, "ember,ys-m33-rtl8723ds-read-probe", 1) == 0);
		if (which == 3) assert(fdt_delprop(fdt, 0, OPT) == 0);
		if (which == 4) assert(fdt_setprop_u32(fdt, host, MARK, 1) == 0);
		if (which == 5) assert(fdt_setprop_string(fdt, host, "status", "disabled") == 0);
		if (which == 6) {
			node = fdt_path_offset(fdt, "/soc@03000000/sdmmc@04022000");
			host = node;
		}
		if (which == 7) assert(fdt_setprop_u32(fdt, host, "reg", 1) == 0);
		if (which == 8) assert(fdt_setprop_string(fdt, 0, "compatible", "foreign,board") == 0);
		if (which == 9) assert(fdt_setprop(fdt, 0, "compatible", "allwinner,a133\0x",
		    sizeof("allwinner,a133\0x") - 1) == 0);
		if (which == 10) assert(fdt_setprop_u32(fdt, 0, OPT, 1) == 0);
		if (which == 11) assert(fdt_delprop(fdt, host, MARK) == 0);
		assert(!rtl8723ds_read_match(fdt, host, 0x024c, 0xd723, 1, 7));
	}
	sdio_fixture(fdt, "allwinner,a133");
	assert(sun50i_a133_fdt_fixup(fdt) == 0);
	assert(fdt_setprop(fdt, 0, "ember,ys-m33-rtl8723ds-read-probe", NULL, 0) == 0);
	host = fdt_path_offset(fdt, HOST);
	assert(!rtl8723ds_read_match(fdt, host, 0x02d0, 0xd723, 1, 7));
	assert(!rtl8723ds_read_match(fdt, host, 0x024c, 0xd724, 1, 7));
	assert(!rtl8723ds_read_match(fdt, host, 0x024c, 0xd723, 0, 7));
	assert(!rtl8723ds_read_match(fdt, host, 0x024c, 0xd723, 2, 7));
	assert(!rtl8723ds_read_match(fdt, host, 0x024c, 0xd723, 1, 0));
	assert(!rtl8723ds_read_match(NULL, host, 0x024c, 0xd723, 1, 7));
	puts("RTL8723DS match: actual host, board/card identity and dual opt-in guards passed");
}

#include "rtl8723ds-match-fixture.h"

static void
native_match_contract(void)
{
	unsigned char fdt[16384];
	struct match_device host = { .name = "sunximmc" };
	struct match_device bus = { .name = "sdmmc", .parent = &host };
	struct sdmmc_softc sc = { .sc_dev = &bus, .sc_flags = SMF_IO_MODE };
	struct sdmmc_function fn0 = { .sc = &sc, .number = 0,
	    .cis = { 0x024c, 0xd723 } };
	struct sdmmc_function fn1 = { .sc = &sc, .number = 1,
	    .cis = { 0xffff, 0xffff } };
	struct sdmmc_attach_args args = { 0xffff, 0xffff, 7, &fn1 };

	sdio_fixture(fdt, "allwinner,a133");
	assert(sun50i_a133_fdt_fixup(fdt) == 0);
	assert(fdt_setprop(fdt, 0, "ember,ys-m33-rtl8723ds-read-probe", NULL, 0) == 0);
	match_fdt = fdt;
	host.handle.type = DEVHANDLE_TYPE_OF;
	host.handle.integer = fdt_path_offset(fdt, HOST) + 1;
	sc.sc_fn0 = &fn0;
	/* NetBSD passes per-function IDs; sdmmc_print uses common FN0 CIS. */
	assert(rtl8723ds_probe_match(&bus, NULL, &args) == 1);
	args.manufacturer = 0x024c; args.product = 0xd723;
	fn0.cis.product = 0xd724;
	assert(rtl8723ds_probe_match(&bus, NULL, &args) == 0);
	fn0.cis.product = 0xd723;
	sc.sc_fn0 = NULL;
	assert(rtl8723ds_probe_match(&bus, NULL, &args) == 0);
	sc.sc_fn0 = &fn0;
	fn0.number = 1;
	assert(rtl8723ds_probe_match(&bus, NULL, &args) == 0);
	fn0.number = 0;
	fn0.sc = NULL;
	assert(rtl8723ds_probe_match(&bus, NULL, &args) == 0);
	fn0.sc = &sc;
	fn0.flags = SFF_ERROR;
	assert(rtl8723ds_probe_match(&bus, NULL, &args) == 0);
	fn0.flags = 0;
	host.handle.type = 0;
	assert(rtl8723ds_probe_match(&bus, NULL, &args) == 0);
	host.handle.type = DEVHANDLE_TYPE_OF;
	host.handle.integer = fdt_path_offset(fdt, "/soc@03000000/sdmmc@04022000") + 1;
	assert(rtl8723ds_probe_match(&bus, NULL, &args) == 0);
	host.handle.integer = fdt_path_offset(fdt, HOST) + 1;
	sc.sc_flags |= SMF_MEM_MODE;
	assert(rtl8723ds_probe_match(&bus, NULL, &args) == 0);
	sc.sc_flags = SMF_IO_MODE;
	fn1.flags = SFF_ERROR;
	assert(rtl8723ds_probe_match(&bus, NULL, &args) == 0);
	fn1.flags = 0;
	assert(rtl8723ds_probe_match(&bus, NULL, NULL) == 0);
	puts("RTL8723DS native match: common CIS, per-function IDs and parent guards passed");
}

int
main(void)
{
	native_match_contract();
	read_contract();
	match_contract();
	return 0;
}
