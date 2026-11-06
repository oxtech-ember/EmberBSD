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
rtw8821c_coex_init(struct rtw88_softc *sc)
{
	struct coex_io c;

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
	coex_modify(&c, 0x0073, 1, 0x04, 0x04); /* Wi-Fi owner */
	/* The donor's zero-initialized POWERON switch state is already BT. */
	coex_write(&c, 0xff1a, 1, 0);
	coex_modify(&c, 0x0064, 4, 0x00100000, 0);
	coex_modify(&c, 0x0064, 4, 0x01000000, 0);
	coex_modify(&c, 0x0040, 4, 0x00080000, 0);
	coex_modify(&c, 0x0064, 4, 0x00008000, 0);
	coex_modify(&c, 0x0070, 4, 0x00040000, 0);
	coex_modify(&c, 0x0070, 4, 0x08000000, 0);
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
	/* WONLY: BT software low, WLAN software high. */
	coex_lte(&c, 0xc000, 0x4000);
	coex_lte(&c, 0x0c00, 0x0400);
	coex_lte(&c, 0x3000, 0x3000);
	coex_lte(&c, 0x0300, 0x0300);
	coex_modify(&c, 0x0073, 1, 0x04, 0x04);
	coex_antenna(&c);
	return coex_check(&c);
}
