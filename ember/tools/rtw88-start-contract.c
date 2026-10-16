/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD RTL8822CE firmware-bootstrap fake-bus tests, AI-assisted. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rtw88-test-kernel.h"
#include <dev/ic/rtw88var.h>
#include <dev/ic/rtw8822c.h>
#include <dev/ic/rtw8822c_table.h>

static unsigned assertions, failures;

#define CHECK(x)                                                               \
	do {                                                                   \
		assertions++;                                                  \
		if (!(x)) {                                                    \
			failures++;                                            \
			fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__,     \
			    #x);                                               \
		}                                                              \
	} while (0)

static struct {
	uint8_t mmio[0x8000];
	uint8_t efuse[512];
	const uint8_t *fw;
	size_t fw_len;
	int fw_error;
	int master;
	int missing;
	int remove;
	int fail_syscfg;
	int fail_efuse;
	int busy_box;
	int fw_never_ready;
	int stop_at_delay;
	unsigned delays;
	unsigned writes;
	unsigned cfg_writes;
	unsigned dma_allocs;
	struct rtw88_softc *sc;
} f;

static const uint8_t fw_header[64] = {
	0x22, 0x88, 0x00, 0x00, 0x09, 0x00, 0x09, 0x0b,
	0x00, 0x00, 0x00, 0x00, 0xe7, 0x01, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x10, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x20, 0x80, 0x80, 0x4b, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x30, 0xd3, 0x00, 0x00, 0xd0, 0xf7, 0x01, 0x00,
	0x00, 0x00, 0x10, 0x80, 0x00, 0x00, 0x03, 0x80
};

static uint8_t fw_image[202456];

/* Pinned Task 2 exact-map fixture: valid MAC and RFE 5. */
static const uint8_t efuse_ok[] = {
	0x8f, 0x48, 0x02, 0x11, 0x22, 0x33, 0x44, 0x55,
	0x2e, 0x81, 0xf7,
	0xef, 0x20, 0x34, 0xfe, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04,
	0x0f, 0x30, 0x01, 0x22, 0x03, 0x45, 0x06, 0x07, 0x08, 0xa9,
	0x2f, 0x38, 0xa9, 0x31, 0x05, 'U', 'S', 0x66,
	0x4f, 0x3e, 0x1a, 0x2b,
	0x8f, 0x4b, 0xaa, 0xbb,
	0xff
};

static int
is_cfg_reg(uint32_t reg)
{

	if (reg == 0x0030 || reg == 0x00cf || reg == 0x00f0)
		return 0;
	return 1;
}

static int
access_ok(uint32_t reg, size_t width)
{

	if (f.remove)
		return ENXIO;
	if (reg + width > sizeof(f.mmio) || (reg & (width - 1)) != 0)
		return EINVAL;
	return 0;
}

static int
read_1(void *cookie, uint32_t reg, uint8_t *out)
{
	int error;

	(void)cookie;
	error = access_ok(reg, 1);
	if (error != 0)
		return error;
	if (f.fail_syscfg && reg == 0x00f0)
		return EIO;
	*out = f.mmio[reg];
	if (reg == 0x06)
		*out |= 0x02;
	if (reg == 0x01cc && f.busy_box)
		*out = 0x01;
	return 0;
}

static int
read_2(void *cookie, uint32_t reg, uint16_t *out)
{
	int error;

	(void)cookie;
	error = access_ok(reg, 2);
	if (error != 0)
		return error;
	if (f.fail_syscfg && reg == 0x00f0)
		return EIO;
	*out = (uint16_t)f.mmio[reg] | (uint16_t)f.mmio[reg + 1] << 8;
	return 0;
}

static int
read_4(void *cookie, uint32_t reg, uint32_t *out)
{
	int error;
	uint32_t v;

	(void)cookie;
	error = access_ok(reg, 4);
	if (error != 0)
		return error;
	if (f.fail_syscfg && reg == 0x00f0)
		return EIO;
	if (f.fail_efuse && reg == 0x0030)
		return EIO;
	v = (uint32_t)f.mmio[reg] | (uint32_t)f.mmio[reg + 1] << 8 |
	    (uint32_t)f.mmio[reg + 2] << 16 | (uint32_t)f.mmio[reg + 3] << 24;
	if (reg == 0x0030) {
		uint32_t addr = (v >> 8) & 0x3ff;

		if (addr < sizeof(f.efuse))
			v = (v & ~0xffu) | f.efuse[addr];
		v |= UINT32_C(1) << 31;
	}
	if (reg == 0x0080 && !f.fw_never_ready)
		v |= 0xc078;
	if (reg == 0x1208)
		v &= ~(UINT32_C(1) << 31);
	*out = v;
	return 0;
}

static int
write_1(void *cookie, uint32_t reg, uint8_t val)
{
	int error;

	(void)cookie;
	error = access_ok(reg, 1);
	if (error != 0)
		return error;
	f.writes++;
	if (is_cfg_reg(reg))
		f.cfg_writes++;
	f.mmio[reg] = val;
	if (reg == 0x05)
		f.mmio[reg] &= (uint8_t)~0x03;
	return 0;
}

static int
write_2(void *cookie, uint32_t reg, uint16_t val)
{
	int error;

	(void)cookie;
	error = access_ok(reg, 2);
	if (error != 0)
		return error;
	f.writes++;
	if (is_cfg_reg(reg))
		f.cfg_writes++;
	f.mmio[reg] = (uint8_t)val;
	f.mmio[reg + 1] = (uint8_t)(val >> 8);
	return 0;
}

static int
write_4(void *cookie, uint32_t reg, uint32_t val)
{
	int error;

	(void)cookie;
	error = access_ok(reg, 4);
	if (error != 0)
		return error;
	f.writes++;
	if (is_cfg_reg(reg))
		f.cfg_writes++;
	f.mmio[reg] = (uint8_t)val;
	f.mmio[reg + 1] = (uint8_t)(val >> 8);
	f.mmio[reg + 2] = (uint8_t)(val >> 16);
	f.mmio[reg + 3] = (uint8_t)(val >> 24);
	return 0;
}

static void
delay_us(void *cookie, unsigned us)
{

	(void)cookie;
	(void)us;
	f.delays++;
	if (f.stop_at_delay != 0 && f.sc != NULL &&
	    f.delays == (unsigned)f.stop_at_delay)
		rtw88_stop(f.sc);
}

static int
dma_alloc(void *cookie, struct rtw88_dma *ring, size_t size)
{

	(void)cookie;
	ring->vaddr = calloc(1, size);
	if (ring->vaddr == NULL)
		return ENOMEM;
	ring->addr = 0x2000 + f.dma_allocs * 0x2000;
	ring->size = size;
	ring->cookie = ring;
	f.dma_allocs++;
	return 0;
}

static void
dma_free(void *cookie, struct rtw88_dma *ring)
{

	(void)cookie;
	free(ring->vaddr);
	memset(ring, 0, sizeof(*ring));
	if (f.dma_allocs > 0)
		f.dma_allocs--;
}

static void
dma_sync(void *cookie, struct rtw88_dma *ring, int ops)
{

	(void)cookie;
	(void)ring;
	(void)ops;
}

static int
intr_establish(void *cookie)
{

	(void)cookie;
	return 0;
}

static void
intr_disestablish(void *cookie)
{

	(void)cookie;
}

static int
firmware_load(void *cookie, const char *drv, const char *name,
    const uint8_t **data, size_t *len)
{

	(void)cookie;
	(void)drv;
	(void)name;
	if (f.fw_error != 0)
		return f.fw_error;
	if (f.fw == NULL)
		return ENOENT;
	*data = f.fw;
	*len = f.fw_len;
	return 0;
}

static void
firmware_release(void *cookie)
{

	(void)cookie;
}

static int
bus_master(void *cookie, int enable)
{

	(void)cookie;
	f.master = enable ? 1 : 0;
	return 0;
}

static const struct rtw88_bus_ops ops = {
	.read_1 = read_1,
	.read_2 = read_2,
	.read_4 = read_4,
	.write_1 = write_1,
	.write_2 = write_2,
	.write_4 = write_4,
	.delay_us = delay_us,
	.dma_alloc = dma_alloc,
	.dma_free = dma_free,
	.dma_sync = dma_sync,
	.intr_establish = intr_establish,
	.intr_disestablish = intr_disestablish,
	.firmware_load = firmware_load,
	.firmware_release = firmware_release,
	.bus_master = bus_master
};

static void
reset_fw(void)
{

	memset(fw_image, 0, sizeof(fw_image));
	memcpy(fw_image, fw_header, sizeof(fw_header));
}

static void
reset_bus(struct rtw88_softc *sc)
{
	unsigned i;

	memset(&f, 0, sizeof(f));
	memset(sc, 0, sizeof(*sc));
	sc->sc_ops = &ops;
	sc->sc_bus = sc;
	sc->sc_vendor = 0x10ec;
	sc->sc_product = 0xc822;
	f.sc = sc;
	f.mmio[0x06] = 0x02;
	f.mmio[0xf0] = 0x00;
	f.mmio[0xf1] = 0x00;
	f.mmio[0xf2] = 0x00;
	f.mmio[0xf3] = 0x08;
	memcpy(f.efuse, efuse_ok, sizeof(efuse_ok));
	for (i = sizeof(efuse_ok); i < sizeof(f.efuse); i++)
		f.efuse[i] = 0xff;
	reset_fw();
	f.fw = fw_image;
	f.fw_len = sizeof(fw_image);
}

static int
attach_ok(struct rtw88_softc *sc)
{

	CHECK(rtw88_core_attach(sc) == 0);
	CHECK(sc->sc_resources_ready);
	CHECK(!sc->sc_rf_enabled && !sc->sc_dma_master);
	return sc->sc_resources_ready;
}

static void
test_missing_firmware(void)
{
	struct rtw88_softc sc;

	reset_bus(&sc);
	f.fw = NULL;
	if (!attach_ok(&sc))
		return;
	CHECK(rtw88_start(&sc) == ENOENT);
	CHECK(!sc.sc_rf_enabled && !sc.sc_dma_master && f.master == 0);
	CHECK(f.cfg_writes == 0);
	rtw88_core_detach(&sc);
	CHECK(f.dma_allocs == 0);
}

static void
test_corrupt_firmware(void)
{
	struct rtw88_softc sc;

	reset_bus(&sc);
	fw_image[0] = 0x00;
	if (!attach_ok(&sc))
		return;
	CHECK(rtw88_start(&sc) != 0);
	CHECK(!sc.sc_rf_enabled && !sc.sc_dma_master && f.master == 0);
	CHECK(f.cfg_writes == 0);
	rtw88_core_detach(&sc);
}

static void
test_failed_identity_read(void)
{
	struct rtw88_softc sc;

	reset_bus(&sc);
	f.fail_syscfg = 1;
	if (!attach_ok(&sc))
		return;
	CHECK(rtw88_start(&sc) == EIO);
	CHECK(!sc.sc_rf_enabled && f.cfg_writes == 0);
	CHECK(f.master == 0);
	rtw88_core_detach(&sc);
}

static void
test_failed_efuse_read(void)
{
	struct rtw88_softc sc;

	reset_bus(&sc);
	f.fail_efuse = 1;
	if (!attach_ok(&sc))
		return;
	CHECK(rtw88_start(&sc) == EIO);
	CHECK(!sc.sc_rf_enabled && f.master == 0);
	rtw88_core_detach(&sc);
}

static void
test_malformed_efuse(void)
{
	struct rtw88_softc sc;

	reset_bus(&sc);
	memset(f.efuse, 0xff, sizeof(f.efuse));
	if (!attach_ok(&sc))
		return;
	CHECK(rtw88_start(&sc) == EINVAL);
	CHECK(!sc.sc_rf_enabled && f.master == 0);
	rtw88_core_detach(&sc);
}

static void
test_unsupported_identity(void)
{
	struct rtw88_softc sc;

	reset_bus(&sc);
	sc.sc_product = 0xb822;
	if (!attach_ok(&sc))
		return;
	CHECK(rtw88_start(&sc) == ENOTSUP);
	CHECK(!sc.sc_rf_enabled && f.master == 0);
	rtw88_core_detach(&sc);
}

static void
test_command_length(void)
{
	struct rtw88_softc sc;
	uint8_t buf[8] = { 0 };

	reset_bus(&sc);
	if (!attach_ok(&sc))
		return;
	CHECK(rtw88_start(&sc) == 0);
	CHECK(sc.sc_rf_enabled);
	CHECK(rtw88_fw_command(&sc, 1, buf, 8) == EMSGSIZE);
	CHECK(rtw88_fw_command(&sc, 1, buf, 7) == 0);
	rtw88_stop(&sc);
	CHECK(!sc.sc_rf_enabled && f.master == 0);
	rtw88_core_detach(&sc);
}

static void
test_command_timeout(void)
{
	struct rtw88_softc sc;
	uint8_t buf[1] = { 0 };

	reset_bus(&sc);
	if (!attach_ok(&sc))
		return;
	CHECK(rtw88_start(&sc) == 0);
	f.busy_box = 1;
	CHECK(rtw88_fw_command(&sc, 1, buf, 1) == ETIMEDOUT);
	rtw88_stop(&sc);
	rtw88_core_detach(&sc);
}

static void
test_stop_during_bootstrap(void)
{
	struct rtw88_softc sc;

	reset_bus(&sc);
	f.stop_at_delay = 1;
	if (!attach_ok(&sc))
		return;
	CHECK(rtw88_start(&sc) != 0);
	CHECK(!sc.sc_rf_enabled && f.master == 0);
	CHECK(!sc.sc_dma_master);
	rtw88_core_detach(&sc);
}

static void
test_stale_generation(void)
{
	struct rtw88_softc sc;
	uint8_t buf[1] = { 0 };
	uint32_t gen;

	reset_bus(&sc);
	if (!attach_ok(&sc))
		return;
	CHECK(rtw88_start(&sc) == 0);
	gen = sc.sc_generation;
	f.stop_at_delay = (int)(f.delays + 1);
	f.busy_box = 1;
	CHECK(rtw88_fw_command(&sc, 1, buf, 1) != 0);
	CHECK(sc.sc_generation != gen || !sc.sc_started);
	rtw88_core_detach(&sc);
}

static void
test_success_consumes_tables(void)
{
	struct rtw88_softc sc;
	static const uint32_t cond[] = {
		0x80000000, 0x00000000,
		0x40000000, 0x00000000,
		0x00001d90, 0x300001ff,
		0xb0000000, 0x00000000
	};
	static const struct rtw8822c_table tiny = {
		.data = cond,
		.count = 8,
		.kind = RTW8822C_TBL_AGC,
		.rf_path = 0
	};

	reset_bus(&sc);
	if (!attach_ok(&sc))
		return;
	CHECK(rtw88_start(&sc) == 0);
	CHECK(sc.sc_rf_enabled && sc.sc_dma_master && f.master == 1);
	CHECK(sc.sc_pg_rows > 0 && sc.sc_pwr_rows > 0);
	CHECK(f.cfg_writes > 0);
	CHECK(rtw8822c_apply_table(&sc, &tiny) == 0);
	rtw88_stop(&sc);
	CHECK(!sc.sc_rf_enabled && f.master == 0);
	CHECK(rtw88_fw_command(&sc, 1, "", 0) == ENXIO);
	rtw88_core_detach(&sc);
	CHECK(f.dma_allocs == 0);
}

int
main(void)
{

	test_missing_firmware();
	test_corrupt_firmware();
	test_failed_identity_read();
	test_failed_efuse_read();
	test_malformed_efuse();
	test_unsupported_identity();
	test_command_length();
	test_command_timeout();
	test_stop_during_bootstrap();
	test_stale_generation();
	test_success_consumes_tables();
	printf("rtw88 start contract: %u assertions, %u failures\n",
	    assertions, failures);
	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
