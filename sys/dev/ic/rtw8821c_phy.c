/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: FreeBSD a143197801c680b958ab662c88cf086668a1958b rtw88. */
#ifdef _KERNEL
#include <sys/types.h>
#include <sys/errno.h>
#else
#include <errno.h>
#include <stdint.h>
#endif
#include "rtw8821c_phy.h"
#include "rtw8821c_table.h"
#include "rtw88_phy_table.h"
struct table_ctx {
	const struct rtw8821c_phy_io *io;
	const struct rtw8821c_phy_profile *profile;
	const struct rtw8821c_table *table;
};
static int check(void *arg)
{
	struct table_ctx *c = arg;
	return c->io->check ? c->io->check(c->io->arg) : 0;
}
static int match(void *arg, uint32_t w, int *out)
{
	struct table_ctx *c = arg;
	unsigned cut = (w >> 24) & 15, pkg = (w >> 12) & 15,
		 intf = (w >> 8) & 15, rfe = w & 255;
	*out =
	    (!cut || cut == (c->profile->cut ? c->profile->cut : 15)) &&
	    (!pkg || pkg == (c->profile->package ? c->profile->package : 15)) &&
	    (!intf || intf == 1) && rfe == c->profile->rfe;
	return 0;
}
static int write_table(void *arg, uint32_t addr, uint32_t data)
{
	struct table_ctx *c = arg;
	const struct rtw8821c_phy_io *io = c->io;
	unsigned us = 0;
	int error;
	switch (c->table->kind) {
	case RTW8821C_TBL_MAC:
		return io->write_1(io->arg, addr, (uint8_t)data);
	case RTW8821C_TBL_AGC:
		return io->write_4(io->arg, addr, data);
	case RTW8821C_TBL_BB:
		switch (addr) {
		case 0xfe:
			us = 50000;
			break;
		case 0xfd:
			us = 5000;
			break;
		case 0xfc:
			us = 1000;
			break;
		case 0xfb:
			us = 50;
			break;
		case 0xfa:
			us = 5;
			break;
		case 0xf9:
			us = 1;
			break;
		default:
			return io->write_4(io->arg, addr, data);
		}
		error = check(c);
		if (error)
			return error;
		io->delay_us(io->arg, us);
		return 0;
	case RTW8821C_TBL_RF:
		if (addr == 0xffe || addr == 0xfe) {
			error = check(c);
			if (error)
				return error;
			io->delay_us(io->arg, addr == 0xffe ? 50000 : 100);
			return 0;
		}
		error = io->write_4(io->arg, 0xc90,
				    ((addr & 0xff) << 20) | (data & 0xfffff));
		if (error)
			return error;
		error = check(c);
		if (error)
			return error;
		io->delay_us(io->arg, 13);
		error = check(c);
		if (error)
			return error;
		io->delay_us(io->arg, 1);
		return 0;
	default:
		return EINVAL;
	}
}
int rtw8821c_load_tables_io(const struct rtw8821c_phy_io *io,
			    const struct rtw8821c_phy_profile *profile)
{
	const struct rtw8821c_table *tables[5];
	const struct rtw88_phy_table_ops ops = {match, write_table, check};
	struct table_ctx ctx;
	unsigned i, n = 0;
	int e;
	if (!io || !profile || !io->write_1 || !io->write_4 || !io->delay_us ||
	    profile->cut > 15 || profile->package > 1 ||
	    (profile->rfe != 0 && profile->rfe != 2 && profile->rfe != 4 &&
	     profile->rfe != 6))
		return EINVAL;
	tables[n++] = &rtw8821c_mac_tbl;
	tables[n++] = &rtw8821c_bb_tbl;
	tables[n++] = &rtw8821c_agc_tbl;
	if (profile->rfe == 2 || profile->rfe == 4)
		tables[n++] = &rtw8821c_agc_btg_type2_tbl;
	tables[n++] = &rtw8821c_rf_a_tbl;
	for (i = 0; i < n; i++)
		if ((e = rtw88_phy_table_validate(tables[i]->data,
						  tables[i]->count)) != 0)
			return e;
	ctx.io = io;
	ctx.profile = profile;
	for (i = 0; i < n; i++) {
		ctx.table = tables[i];
		if ((e = rtw88_phy_table_apply(
			 tables[i]->data, tables[i]->count, &ops, &ctx)) != 0)
			return e;
	}
	return 0;
}

#ifdef _KERNEL
#include <dev/ic/rtw88var.h>
int rtw8821c_phy_check(struct rtw8821c_phy *ctx)
{
	if (!ctx || !ctx->sc)
		return EINVAL;
	if (ctx->sc->sc_stop_requested)
		return EINTR;
	return ctx->check ? ctx->check(ctx->check_cookie) : 0;
}
static int native_write1(void *p, uint32_t addr, uint8_t value)
{
	struct rtw8821c_phy *ctx = p;
	struct rtw88_softc *sc = ctx->sc;
	return sc->sc_ops->write_1(sc->sc_bus, addr, value);
}
static int native_write4(void *p, uint32_t addr, uint32_t value)
{
	struct rtw8821c_phy *ctx = p;
	struct rtw88_softc *sc = ctx->sc;
	return sc->sc_ops->write_4(sc->sc_bus, addr, value);
}
static void native_delay(void *p, unsigned us)
{
	struct rtw8821c_phy *ctx = p;
	struct rtw88_softc *sc = ctx->sc;
	sc->sc_ops->delay_us(sc->sc_bus, us);
}
static int native_check(void *p)
{
	struct rtw8821c_phy *ctx = p;
	return rtw8821c_phy_check(ctx);
}
int rtw8821c_load_tables(struct rtw8821c_phy *ctx)
{
	struct rtw88_softc *sc;
	struct rtw8821c_phy_io io;
	struct rtw8821c_phy_profile profile;
	if (!ctx || !ctx->sc)
		return EINVAL;
	sc = ctx->sc;
	io = (struct rtw8821c_phy_io){ctx, native_write1, native_write4,
				      native_delay, native_check};
	profile = (struct rtw8821c_phy_profile){
	    sc->sc_cut, ctx->efuse.package_type, ctx->efuse.rfe_option};
	return rtw8821c_load_tables_io(&io, &profile);
}
#endif
#ifdef _KERNEL
static int phy_mask32(struct rtw8821c_phy *ctx, uint32_t addr, uint32_t mask,
		      uint32_t value)
{
	struct rtw88_softc *sc = ctx->sc;
	uint32_t reg;
	int error;
	if ((error = rtw8821c_phy_check(ctx)) != 0)
		return error;
	error = sc->sc_ops->read_4(sc->sc_bus, addr, &reg);
	if (error != 0)
		return error;
	if ((error = rtw8821c_phy_check(ctx)) != 0)
		return error;
	reg = (reg & ~mask) | ((value << __builtin_ctz(mask)) & mask);
	return sc->sc_ops->write_4(sc->sc_bus, addr, reg);
}
#define PHY_TRY(expr)                                                          \
	do {                                                                   \
		if ((error = rtw8821c_phy_check(ctx)) != 0)                    \
			return error;                                          \
		error = (expr);                                                \
		if (error != 0)                                                \
			return error;                                          \
	} while (0)
int rtw8821c_phy_pre(struct rtw8821c_phy *ctx)
{
	struct rtw88_softc *sc;
	uint8_t val;
	int error;
	if (!ctx || !ctx->sc)
		return EINVAL;
	sc = ctx->sc;
	ctx->post_valid = 0;
	if ((error = rtw8821c_phy_check(ctx)) != 0)
		return error;
	PHY_TRY(sc->sc_ops->read_1(sc->sc_bus, 0x2, &val));
	val |= 0x40;
	PHY_TRY(sc->sc_ops->write_1(sc->sc_bus, 0x2, val));
	val |= 0x03;
	PHY_TRY(sc->sc_ops->write_1(sc->sc_bus, 0x2, val));
	val &= (uint8_t)~0x03;
	PHY_TRY(sc->sc_ops->write_1(sc->sc_bus, 0x2, val));
	val |= 0x03;
	PHY_TRY(sc->sc_ops->write_1(sc->sc_bus, 0x2, val));
	PHY_TRY(sc->sc_ops->write_1(sc->sc_bus, 0x1f, 7));
	sc->sc_ops->delay_us(sc->sc_bus, 10);
	if ((error = rtw8821c_phy_check(ctx)) != 0)
		return error;
	PHY_TRY(sc->sc_ops->write_1(sc->sc_bus, 0xef, 7));
	sc->sc_ops->delay_us(sc->sc_bus, 10);
	return phy_mask32(ctx, 0x808, 0x30000000, 0);
}
int rtw8821c_phy_post(struct rtw8821c_phy *ctx)
{
	static const uint16_t scales[] = {
	    0x081, 0x088, 0x090, 0x099, 0x0a2, 0x0ac, 0x0b6, 0x0c0,
	    0x0cc, 0x0d8, 0x0e5, 0x0f2, 0x101, 0x110, 0x120, 0x131,
	    0x143, 0x156, 0x16a, 0x180, 0x197, 0x1af, 0x1c8, 0x1e3,
	    0x200, 0x21e, 0x23e, 0x261, 0x285, 0x2ab, 0x2d3, 0x2fe,
	    0x32b, 0x35c, 0x38e, 0x3c4, 0x3fe};
	struct rtw88_softc *sc;
	uint32_t val, defaults[3];
	uint8_t cck, crystal;
	unsigned i;
	int error;
	if (!ctx || !ctx->sc)
		return EINVAL;
	sc = ctx->sc;
	ctx->post_valid = 0;
	/* An unprogrammed crystal cap uses the donor default of zero. */
	crystal = ctx->efuse.xtal == 0xff ? 0 : ctx->efuse.xtal & 0x3f;
	PHY_TRY(phy_mask32(ctx, 0x24, 0x7e000000, crystal));
	PHY_TRY(phy_mask32(ctx, 0x28, 0x7e, crystal));
	PHY_TRY(phy_mask32(ctx, 0xa2c, (1U << 18) | (1U << 22), 0));
	PHY_TRY(phy_mask32(ctx, 0x808, 0x30000000, 3));
	if ((error = rtw8821c_phy_check(ctx)) != 0)
		return error;
	PHY_TRY(sc->sc_ops->read_4(sc->sc_bus, 0xa24, &defaults[0]));
	PHY_TRY(sc->sc_ops->read_4(sc->sc_bus, 0xa28, &defaults[1]));
	PHY_TRY(sc->sc_ops->read_4(sc->sc_bus, 0xaac, &defaults[2]));
	PHY_TRY(sc->sc_ops->read_1(sc->sc_bus, 0xaaa, &cck));
	PHY_TRY(sc->sc_ops->read_4(sc->sc_bus, 0xc1c, &val));
	val = (val & 0xffe00000) >> 21;
	for (i = 0; i < sizeof(scales) / sizeof(scales[0]); i++)
		if (scales[i] == val)
			break;
	/* BF defaults from rtw_bf_phy_init. */
	PHY_TRY(phy_mask32(ctx, 0x14c0, 0x0001f0bf, 0x1a000 >> 0));
	if ((error = rtw8821c_phy_check(ctx)) != 0)
		return error;
	PHY_TRY(sc->sc_ops->write_1(sc->sc_bus, 0x167c, 0x70));
	PHY_TRY(sc->sc_ops->write_2(sc->sc_bus, 0x1680, 0));
	PHY_TRY(phy_mask32(ctx, 0x42c, 1U << 30, 1));
	PHY_TRY(sc->sc_ops->write_1(sc->sc_bus, 0x45f, 0x10));
	PHY_TRY(phy_mask32(ctx, 0x6dc, 0x3f000000, 4));
	PHY_TRY(sc->sc_ops->write_4(sc->sc_bus, 0x1c94, 0xafffafff));
	if ((error = rtw8821c_phy_check(ctx)) != 0)
		return error;
	ctx->channel_defaults[0] = defaults[0];
	ctx->channel_defaults[1] = defaults[1];
	ctx->channel_defaults[2] = defaults[2];
	ctx->cck_pd_default = cck & 0x1f;
	ctx->default_ofdm_index =
	    i < sizeof(scales) / sizeof(scales[0]) ? i : 24;
	ctx->post_valid = 1;
	return 0;
}
#undef PHY_TRY
#endif
#ifdef _KERNEL
#include <dev/ic/rtw88_hw.h>
static int phy_mask8(struct rtw8821c_phy *ctx, uint32_t addr, uint8_t mask,
		     uint8_t value)
{
	struct rtw88_softc *sc = ctx->sc;
	uint8_t old;
	int e;
	if ((e = rtw8821c_phy_check(ctx)) != 0)
		return e;
	e = sc->sc_ops->read_1(sc->sc_bus, addr, &old);
	if (e)
		return e;
	if ((e = rtw8821c_phy_check(ctx)) != 0)
		return e;
	return sc->sc_ops->write_1(sc->sc_bus, addr,
				   (old & (uint8_t)~mask) | (value & mask));
}
static int phy_rf_read(struct rtw8821c_phy *ctx, unsigned addr, uint32_t *out)
{
	struct rtw88_softc *sc = ctx->sc;
	uint32_t value;
	int e;
	if ((e = rtw8821c_phy_check(ctx)) != 0)
		return e;
	e = sc->sc_ops->read_4(sc->sc_bus, 0x2800 + ((addr & 0xff) << 2),
			       &value);
	if (e != 0)
		return e;
	e = rtw8821c_phy_check(ctx);
	if (e != 0)
		return e;
	*out = value & 0xfffff;
	return 0;
}
int rtw8821c_read_rf(struct rtw8821c_phy *ctx, unsigned addr, uint32_t *value)
{
	if (!ctx || !ctx->sc || !value || addr > 0xff)
		return EINVAL;
	return phy_rf_read(ctx, addr, value);
}
static int phy_rf_write(struct rtw8821c_phy *ctx, unsigned addr, uint32_t value)
{
	struct rtw88_softc *sc = ctx->sc;
	int e;
	if ((e = rtw8821c_phy_check(ctx)) != 0)
		return e;
	e = sc->sc_ops->write_4(sc->sc_bus, 0xc90,
				((addr & 0xff) << 20) | (value & 0xfffff));
	if (e == 0)
		e = rtw8821c_phy_check(ctx);
	if (e == 0)
		sc->sc_ops->delay_us(sc->sc_bus, 13);
	return e;
}
static int phy_rf_mask(struct rtw8821c_phy *ctx, unsigned addr, uint32_t mask,
		       uint32_t value)
{
	uint32_t old;
	int e;
	e = phy_rf_read(ctx, addr, &old);
	if (e)
		return e;
	return phy_rf_write(
	    ctx, addr, (old & ~mask) | ((value << __builtin_ctz(mask)) & mask));
}
int rtw8821c_write_rf_mask(struct rtw8821c_phy *ctx, unsigned addr,
			   uint32_t mask, uint32_t value)
{
	if (!ctx || !ctx->sc || addr > 0xff || !mask || (mask & ~0xfffffU) ||
	    (value & ~(mask >> __builtin_ctz(mask))))
		return EINVAL;
	if (mask == 0xfffffU)
		return phy_rf_write(ctx, addr, value);
	return phy_rf_mask(ctx, addr, mask, value);
}
#define CH_TRY(expr)                                                           \
	do {                                                                   \
		if ((error = rtw8821c_phy_check(ctx)) != 0)                    \
			return error;                                          \
		error = (expr);                                                \
		if (error)                                                     \
			return error;                                          \
	} while (0)
int rtw8821c_set_channel_tuple(struct rtw8821c_phy *ctx,
			       const struct rtw88_channel_tuple *tuple)
{
	static const uint16_t swing[4] = {0x200, 0x16a, 0x101, 0x0b6};
	struct rtw88_softc *sc;
	uint32_t rf, rfctl, val;
	unsigned bb_swing;
	int error, btg;
	if (!ctx || !ctx->sc || !tuple)
		return EINVAL;
	if (!ctx->post_valid || ctx->srrc || tuple->width_mhz != 20 ||
	    tuple->primary != tuple->center || tuple->primary < 1 ||
	    tuple->primary > 13)
		return ENOTSUP;
	if (ctx->efuse.rfe_option != 0 && ctx->efuse.rfe_option != 2 &&
	    ctx->efuse.rfe_option != 4 && ctx->efuse.rfe_option != 6)
		return ENOTSUP;
	sc = ctx->sc;
	btg = (ctx->efuse.rfe_option == 2 || ctx->efuse.rfe_option == 4);
	CH_TRY(phy_mask32(ctx, 0x808, 1U << 28, 1));
	CH_TRY(phy_mask32(ctx, 0x454, 1U << 7, 0));
	CH_TRY(phy_mask32(ctx, 0xa80, 1U << 18, 0));
	CH_TRY(phy_mask32(ctx, 0x814, 0xfc00, 15));
	CH_TRY(phy_mask32(ctx, 0xc1c, 0xf00, 0));
	CH_TRY(phy_mask32(ctx, 0x860, 0x1ffe0000, 0x96a));
	CH_TRY(
	    sc->sc_ops->write_4(sc->sc_bus, 0xa24, ctx->channel_defaults[0]));
	CH_TRY(
	    phy_mask32(ctx, 0xa28, 0xffff, ctx->channel_defaults[1] & 0xffff));
	CH_TRY(
	    sc->sc_ops->write_4(sc->sc_bus, 0xaac, ctx->channel_defaults[2]));
	CH_TRY(sc->sc_ops->read_4(sc->sc_bus, 0x8ac, &val));
	val = (val & 0xffcffc00) | 0x10010000;
	CH_TRY(sc->sc_ops->write_4(sc->sc_bus, 0x8ac, val));
	CH_TRY(phy_mask32(ctx, 0x8c4, 1U << 30, 1));
	bb_swing = ctx->efuse.logical[0xc6];
	if (bb_swing > 9)
		bb_swing = 0;
	CH_TRY(phy_mask32(ctx, 0xc1c, 0xffe00000, swing[bb_swing / 3]));
	CH_TRY(sc->sc_ops->write_1(sc->sc_bus, 0x483, 0));
	CH_TRY(phy_mask32(ctx, 0x668, 0x180, 0));
	CH_TRY(phy_mask32(ctx, 0x24, 0x300000, 0));
	CH_TRY(sc->sc_ops->write_1(sc->sc_bus, 0x55c, 80));
	CH_TRY(sc->sc_ops->write_1(sc->sc_bus, 0x638, 80));
	CH_TRY(phy_mask8(ctx, 0x454, 1U << 7, 0));
	CH_TRY(phy_rf_read(ctx, 0x18, &rf));
	rf = (rf & ~0x70fffU) | 0xc00 | tuple->primary;
	CH_TRY(phy_mask32(ctx, 0x1080, 1U << 16, 1));
	CH_TRY(phy_mask32(ctx, 0, 1U << 26, 1));
	CH_TRY(sc->sc_ops->read_4(sc->sc_bus, 0xcb8, &rfctl));
	rfctl &= ~((1U << 16) | (1U << 18) | (1U << 20) | (1U << 21) |
		   (1U << 22) | (1U << 23));
	rfctl |= btg ? (1U << 16) : ((1U << 20) | (1U << 21) | (1U << 22));
	CH_TRY(phy_mask32(ctx, 0xa84, 0xff0000, btg ? 0x0e : 0x12));
	CH_TRY(phy_mask32(ctx, 0xa80, 0xffff, btg ? 0xfc84 : 0x7532));
	CH_TRY(sc->sc_ops->write_4(sc->sc_bus, 0xcb8, rfctl));
	CH_TRY(phy_rf_mask(ctx, 0xdf, 1U << 6, 1));
	CH_TRY(phy_rf_mask(ctx, 0x64, 0xf, 0xf));
	CH_TRY(phy_rf_write(ctx, 0x18, rf));
	CH_TRY(phy_rf_mask(ctx, 0xb8, 1U << 19, 0));
	CH_TRY(phy_rf_mask(ctx, 0xb8, 1U << 19, 1));
	CH_TRY(phy_mask32(ctx, 0x948, 0x30000000, 2));
	CH_TRY(phy_mask32(ctx, 0x94c, 0x30000000, 2));
	CH_TRY(phy_mask32(ctx, 0xc20, 1U << 31, 1));
	CH_TRY(phy_mask32(ctx, 0x8f0, 1U << 31, 0));
	return 0;
}
#undef CH_TRY
#endif
#ifdef _KERNEL
int rtw8821c_calibrate(struct rtw8821c_phy *ctx)
{
	struct rtw88_softc *sc;
	uint8_t pkt[32] = {1, 0xff, 0x0e, 0, 9, 0, 0, 0};
	uint32_t value, status;
	unsigned i;
	int error;
	if (!ctx || !ctx->sc || !ctx->command || !ctx->wait_ms ||
	    ctx->associated > 1)
		return EINVAL;
	sc = ctx->sc;
	if ((error = rtw8821c_phy_check(ctx)) != 0)
		return error;
	pkt[8] = ctx->associated ? 2 : 0;
	error = ctx->command(ctx->command_cookie, pkt, sizeof(pkt));
	if (error != 0)
		return error;
	for (i = 0; i < 300; i++) {
		error = phy_rf_read(ctx, 0x08, &value);
		if (error != 0)
			return error;
		if (value == 0xabcde)
			break;
		if ((error = rtw8821c_phy_check(ctx)) != 0)
			return error;
		error = ctx->wait_ms(ctx->wait_cookie, 20);
		if (error != 0)
			return error;
	}
	error = phy_rf_write(ctx, 0x08, 0);
	if (error != 0)
		return error;
	if (i == 300)
		return ETIMEDOUT;
	if ((error = rtw8821c_phy_check(ctx)) != 0)
		return error;
	error = sc->sc_ops->read_4(sc->sc_bus, 0x1bf0, &status);
	if (error != 0)
		return error;
	if ((error = rtw8821c_phy_check(ctx)) != 0)
		return error;
	if (status & 0xff)
		return EIO;
	return 0;
}
#endif
