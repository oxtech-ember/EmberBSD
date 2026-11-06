/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: FreeBSD a143197801c680b958ab662c88cf086668a1958b rtw88. */
/* PCIe Wi-Fi-only power-on and WONLY antenna sequence; no BT policy. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <dev/ic/rtw88var.h>
#include <dev/ic/rtw88_io.h>
#include <dev/ic/rtw88_wcpu.h>
#include <dev/ic/rtw8821c_coex.h>

struct coex_io {
	struct rtw88_softc *sc;
	struct rtw88_io io;
	uint32_t generation;
};

static int
coex_check(struct coex_io *coex)
{
	if (coex->io.error == 0 && (coex->sc->sc_stop_requested ||
	    coex->sc->sc_generation != coex->generation))
		coex->io.error = EINTR;
	return coex->io.error;
}

static void
coex_modify(struct coex_io *coex, uint32_t reg, unsigned width,
    uint32_t mask, uint32_t value)
{
	uint32_t old;
	if (coex_check(coex) != 0)
		return;
	old = rtw88_io_rd(&coex->io, reg, width);
	if (coex_check(coex) != 0)
		return;
	rtw88_io_wr(&coex->io, reg, width, (old & ~mask) | (value & mask));
	(void)coex_check(coex);
}

static void
coex_write(struct coex_io *coex, uint32_t reg, unsigned width, uint32_t value)
{
	if (coex_check(coex) != 0)
		return;
	rtw88_io_wr(&coex->io, reg, width, value);
	(void)coex_check(coex);
}

static void
coex_lte(struct coex_io *coex, uint32_t mask, uint32_t value)
{
	if (coex_check(coex) != 0)
		return;
	coex->io.error = rtw88_wcpu_lte_update(coex->sc, coex->generation,
	    0x38, mask, value);
	(void)coex_check(coex);
}

/* Only PHY-supported 8821C RFE profiles are admitted. */
static uint32_t
coex_read(struct coex_io *c, uint32_t reg, unsigned width)
{
	uint32_t value;

	if (coex_check(c) != 0)
		return 0;
	value = rtw88_io_rd(&c->io, reg, width);
	(void)coex_check(c);
	return value;
}

static void
coex_shared_table(struct coex_io *c, int shared)
{
	if (!shared)
		return;
	coex_write(c, 0x06c0, 4, UINT32_C(0x55555555));
	coex_write(c, 0x06c4, 4, UINT32_C(0x55555555));
	coex_write(c, 0x06c8, 4, UINT32_C(0xf0ffffff));
}

/* Donor waits for RFK only when WLAN and BT share the BTG path. */
static void
coex_rfk_wait(struct coex_io *c)
{
	unsigned i;

	if (c->sc->sc_rfe != 2 && c->sc->sc_rfe != 4)
		return;
	for (i = 0; i < 60 && coex_check(c) == 0; i++) {
		uint32_t bt = coex_read(c, 0x00aa, 2);
		uint32_t wl = coex_read(c, 0x049c, 1);

		if (coex_check(c) != 0 || ((bt & 0x20) == 0 && (wl & 1) == 0))
			return;
		c->sc->sc_ops->delay_us(c->sc->sc_bus, 10000);
	}
}

static void
coex_command(struct coex_io *c, uint8_t id, const uint8_t *data, size_t size)
{
	if (coex_check(c) != 0)
		return;
	c->io.error = rtw88_wcpu_boot_command(c->sc, id, data, size);
	(void)coex_check(c);
}

static void
coex_antenna(struct coex_io *coex)
{
	/* The pinned 8821C WONLY trace selects 2 for RFE 0, 2 and 4;
	 * RFE 6 has no external antenna switch. */
	if (coex->sc->sc_rfe == 6)
		return;
	coex_modify(coex, 0x004c, 4, 0x00800000, 0); /* DPDT select */
	coex_modify(coex, 0x004c, 4, 0x01000000, 0x01000000);
	coex_modify(coex, 0x0cb4, 1, 0xff, 0x77); /* BB switch pins */
	coex_modify(coex, 0x0cb4, 4, 0xf0000000, UINT32_C(0x20000000));
	coex_modify(coex, 0x0067, 1, 0x20, 0x20);
	coex_modify(coex, 0x0067, 1, 0x10, 0x10);
}

int
rtw8821c_coex_init(struct rtw88_softc *sc, uint8_t bt_setting)
{
	struct coex_io c;
	const uint8_t disable_extend[6] = { 0x0c, 0x01, 0, 0, 0, 0 };
	const uint8_t tdma_off[5] = { 0, 0, 0, 0, 0 };
	int shared = (bt_setting & 1) != 0;

	if (sc == NULL || sc->sc_ops == NULL)
		return EINVAL;
	if (sc->sc_rfe != 0 && sc->sc_rfe != 2 && sc->sc_rfe != 4 &&
	    sc->sc_rfe != 6)
		return ENOTSUP;
	c.sc = sc;
	c.generation = sc->sc_generation;
	rtw88_io_init(&c.io, sc->sc_ops, sc->sc_bus);
	if (coex_check(&c) != 0)
		return c.io.error;
	/* Power-on Wi-Fi-only setup, then donor WONLY initialization. */
	coex_modify(&c, 0x0002, 1, 0x03, 0x03);
	(void)coex_read(&c, 0x00aa, 2);
	coex_rfk_wait(&c);
	coex_modify(&c, 0x0073, 1, 0x04, 0x04); /* Wi-Fi owner */
	coex_shared_table(&c, shared);
	/* The donor's zero-initialized POWERON switch state is already BT. */
	coex_write(&c, 0xff1a, 1, 0);
	coex_modify(&c, 0x0064, 4, 0x00100000, 0);
	coex_modify(&c, 0x0064, 4, 0x01000000, 0);
	coex_modify(&c, 0x0040, 4, 0x00080000, 0);
	coex_modify(&c, 0x0064, 4, 0x00008000, 0);
	coex_modify(&c, 0x0070, 4, 0x00040000, 0);
	coex_modify(&c, 0x0070, 4, 0x08000000, 0);
	(void)coex_read(&c, 0x00f1, 1);
	(void)coex_read(&c, 0x00aa, 2);
	coex_command(&c, 0x69, disable_extend, sizeof(disable_extend));
	coex_modify(&c, 0x0550, 1, 0x08, 0x08);
	coex_modify(&c, 0x0550, 1, 0x08, 0x08);
	coex_modify(&c, 0x0790, 1, 0x3f, 0x05);
	coex_write(&c, 0x0778, 1, 0x01);
	coex_modify(&c, 0x0040, 4, 0x20, 0x20);
	coex_modify(&c, 0x0040, 4, 0x200, 0x200);
	coex_modify(&c, 0x04c6, 1, 0x10, 0x10);
	coex_modify(&c, 0x04c6, 1, 0x20, 0);
	coex_modify(&c, 0x0762, 2, 0x1000, 0x1000);
	coex_modify(&c, 0x06cf, 1, 0x08, 0x08);
	coex_modify(&c, 0x06cc, 1, 0x08, 0x08);
	coex_modify(&c, 0x06cc, 1, 0x10, 0x10);
	coex_modify(&c, 0x06cf, 1, 0x08, 0x08);
	coex_rfk_wait(&c);
	/* WONLY: BT software low, WLAN software high. */
	coex_lte(&c, 0xc000, 0x4000);
	coex_lte(&c, 0x0c00, 0x0400);
	coex_lte(&c, 0x3000, 0x3000);
	coex_lte(&c, 0x0300, 0x0300);
	coex_modify(&c, 0x0073, 1, 0x04, 0x04);
	coex_antenna(&c);
	coex_write(&c, 0x00aa, 2, 0x8003); /* ACTIVE|ONOFF, BT interrupt */
	coex_shared_table(&c, shared);
	if (shared)
		coex_command(&c, 0x60, tdma_off, sizeof(tdma_off));
	return coex_check(&c);
}
