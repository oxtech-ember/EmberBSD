/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: FreeBSD a143197801c680b958ab662c88cf086668a1958b rtw88. */
/*
 * Wi-Fi-only register slices from coex.c, rtw8822c.c and util.c.
 * All supported RTL8822C RFE profiles have no external antenna switch.
 * WONLY keeps GNT_BT SW-low, GNT_WL SW-high and WL path ownership on both
 * bands. The reference stops coex DM after WONLY, so its band-switch
 * notification has no register effects. Do not replace WONLY with the
 * dynamic 2G PTA phase when Bluetooth is not managed by this driver.
 *
 * This is not the complete Bluetooth engine. BT discovery, RFK waiting,
 * scoreboard state, coexistence tables, firmware slot-extension/TDMA,
 * BT queries, monitoring and runtime coexistence policy are not ported.
 * No Bluetooth RF calibration may run concurrently with this consumer.
 * The caller serializes the functions with power/channel changes.
 * Reference LTE waits are bounded; unlike its void accessors, this port
 * returns errors and never feeds a failed read into a computed write.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <dev/ic/rtw88var.h>
#include <dev/ic/rtw8822c.h>
#include <dev/ic/rtw88_io.h>

#define LTE_CTRL 0x1700U
#define LTE_WDATA 0x1704U
#define LTE_RDATA 0x1708U
#define LTE_READY UINT32_C(0x20000000)
#define LTE_COEX_CTRL 0x38U
#define LTE_WL_TRX_CTRL 0xa0U
#define LTE_BT_TRX_CTRL 0xa4U

/* util.c check_hw_ready: 1000 polls, 10 us after each failed poll. */
static void
lte_wait(struct rtw88_io *io)
{
	unsigned int i;

	for (i = 0; i < 1000; i++) {
		uint32_t value = rtw88_io_rd(io, LTE_CTRL, 4);

		if (io->error != 0 || (value & LTE_READY) != 0)
			return;
		rtw88_io_delay(io, 10);
	}
	io->error = ETIMEDOUT;
}

/* coex.c write_indirect_reg, with util.c's ordered read/write transport. */
static void
lte_mask(struct rtw88_io *io, uint16_t addr, uint32_t mask, uint32_t value)
{
	uint32_t old;

	if (io->error != 0)
		return;
	lte_wait(io);
	rtw88_io_wr(io, LTE_CTRL, 4, UINT32_C(0x800f0000) | addr);
	old = rtw88_io_rd(io, LTE_RDATA, 4);
	if (io->error != 0)
		return;
	lte_wait(io);
	rtw88_io_wr(io, LTE_WDATA, 4,
	    (old & ~mask) | ((value << __builtin_ctz(mask)) & mask));
	rtw88_io_wr(io, LTE_CTRL, 4, UINT32_C(0xc00f0000) | addr);
}

/* rtw8822c_coex_cfg_rfe_type: RFE0..6 use the same LTE operations. */
static void
rfe_init(struct rtw88_io *io)
{

	lte_mask(io, LTE_COEX_CTRL, 0x80, 0);
	lte_mask(io, LTE_WL_TRX_CTRL, 0xffff, 0xffff);
	lte_mask(io, LTE_BT_TRX_CTRL, 0xffff, 0xffff);
}

/* rtw_coex_coex_ctrl_owner(true); RTL8822C has no additional btg_reg. */
static void
wl_owner(struct rtw88_io *io)
{

	rtw88_io_set(io, 0x0073, 1, 0x04);
}

/* rtw8822c_coex_cfg_gnt_debug: disable the debug GPIO functions. */
static void
gnt_debug(struct rtw88_io *io)
{

	rtw88_io_clr(io, 0x0066, 1, 0x10);
	rtw88_io_clr(io, 0x0067, 1, 0x01);
	rtw88_io_clr(io, 0x0042, 1, 0x08);
	rtw88_io_clr(io, 0x0065, 1, 0x80);
	rtw88_io_clr(io, 0x0073, 1, 0x08);
}

/* rtw8822c_coex_cfg_init. */
static void
hw_init(struct rtw88_io *io)
{
	uint32_t value;

	rtw88_io_set(io, 0x0550, 1, 0x08);
	value = rtw88_io_rd(io, 0x0790, 1);
	rtw88_io_wr(io, 0x0790, 1, (value & ~0x3fU) | 0x05);
	rtw88_io_wr(io, 0x0778, 1, 0x01);
	rtw88_io_set(io, 0x0040, 4, 0x20);
	rtw88_io_set(io, 0x0040, 4, 0x200);
	rtw88_io_set(io, 0x04c6, 1, 0x10);
	rtw88_io_clr(io, 0x04c6, 1, 0x20);
	rtw88_io_set(io, 0x0762, 2, 0x1000);
	rtw88_io_clr(io, 0x04fc, 1, 0x03);
	/* RF path B, RF_MODOPT=1: direct map, preserve non-RF bits. */
	rtw88_io_wr_mask(io, 0x4c04, 0xfffff, 0x40000);
	rtw88_io_delay(io, 1);
}

static int
validate(const struct rtw88_softc *sc)
{

	if (sc == NULL || sc->sc_ops == NULL ||
	    sc->sc_ops->read_1 == NULL || sc->sc_ops->read_2 == NULL ||
	    sc->sc_ops->read_4 == NULL || sc->sc_ops->write_1 == NULL ||
	    sc->sc_ops->write_2 == NULL || sc->sc_ops->write_4 == NULL ||
	    sc->sc_ops->delay_us == NULL)
		return EINVAL;
	if (sc->sc_rfe > 6)
		return ENOTSUP;
	return 0;
}

int
rtw8822c_coex_power_on(struct rtw88_softc *sc)
{
	struct rtw88_io io;
	int error;

	if ((error = validate(sc)) != 0)
		return error;
	rtw88_io_init(&io, sc->sc_ops, sc->sc_bus);
	/* Selected power_on_setting register slice, BT disabled by policy. */
	rtw88_io_set(&io, 0x0002, 1, 0x03);
	rfe_init(&io);
	wl_owner(&io);
	rtw88_io_wr(&io, 0xff1a, 1, 0);
	gnt_debug(&io);
	return io.error;
}

int
rtw8822c_coex_init(struct rtw88_softc *sc)
{
	struct rtw88_io io;
	int error;

	if ((error = validate(sc)) != 0)
		return error;
	rtw88_io_init(&io, sc->sc_ops, sc->sc_bus);
	/* Selected init_hw_config register slice and WONLY antenna phase. */
	rtw88_io_set(&io, 0x0550, 1, 0x08);
	rfe_init(&io);
	hw_init(&io);
	/* TX responses, beacons and the beacon queue get high priority. */
	rtw88_io_set(&io, 0x06cc, 1, 0x08);
	rtw88_io_set(&io, 0x06cc, 1, 0x10);
	rtw88_io_set(&io, 0x06cf, 1, 0x08);
	/* rtw_coex_set_ant_path(COEX_SET_ANT_WONLY). */
	lte_mask(&io, LTE_COEX_CTRL, 0xc000, 1);
	lte_mask(&io, LTE_COEX_CTRL, 0x0c00, 1);
	lte_mask(&io, LTE_COEX_CTRL, 0x3000, 3);
	lte_mask(&io, LTE_COEX_CTRL, 0x0300, 3);
	wl_owner(&io);
	return io.error;
}
