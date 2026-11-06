/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: FreeBSD a143197801c680b958ab662c88cf086668a1958b rtw88/phy.c. */
#ifdef _KERNEL
#include <sys/types.h>
#include <sys/errno.h>
#include <sys/systm.h>
#else
#include <stdint.h>
#include <errno.h>
#include <string.h>
#endif
#include "rtw8821c_phy.h"
#include "rtw8821c_table.h"
#include "rtw88_hw.h"

/* Legacy and HT 1SS, 20 MHz, 2.4 GHz. No output on an incomplete input. */
int rtw8821c_power_indices(const struct rtw8821c_efuse *efuse,
			   const struct rtw88_channel_tuple *tuple,
			   unsigned regd, uint8_t out[20])
{
	const struct rtw8821c_pg_pair *pg = rtw8821c_bb_pg_type0_tbl.data;
	const struct rtw8821c_limit_pair *lim =
	    rtw8821c_txpwr_lmt_type0_tbl.data;
	uint8_t result[20];
	int values[20], limits[3] = {127, 127, 127}, base[3];
	unsigned i, j, group, section, seen = 0, limit_seen = 0;
	const uint8_t *cal;
	int offset, value, diff;
	if (!efuse || !tuple || !out || tuple->width_mhz != 20 ||
	    tuple->primary != tuple->center || tuple->primary < 1 ||
	    tuple->primary > 13 || regd > 12)
		return EINVAL;
	if (efuse->rfe_option != 0 && efuse->rfe_option != 2 &&
	    efuse->rfe_option != 4 && efuse->rfe_option != 6)
		return ENOTSUP;
	group = tuple->primary <= 2    ? 0
		: tuple->primary <= 5  ? 1
		: tuple->primary <= 8  ? 2
		: tuple->primary <= 11 ? 3
				       : 4;
	cal = efuse->logical + 0x10 +
	      ((efuse->rfe_option == 2 || efuse->rfe_option == 4) ? 42 : 0);
	if (cal[group] == 0xff || cal[6 + group] == 0xff || cal[11] == 0xff ||
	    cal[group] > 63 || cal[6 + group] > 63)
		return EINVAL;
	for (i = 0; i < rtw8821c_bb_pg_type0_tbl.count; i++) {
		unsigned start;
		if (pg[i].band != 0 || pg[i].rf_path != 0 ||
		    pg[i].addr < 0xc20 || pg[i].addr > 0xc30 ||
		    (pg[i].addr & 3))
			continue;
		if (pg[i].bitmask != UINT32_MAX)
			return EINVAL;
		start = pg[i].addr - 0xc20;
		for (j = 0; j < 4; j++) {
			unsigned byte = (pg[i].data >> (8 * j)) & 0xff;
			if ((byte & 15) > 9 || (byte >> 4) > 9)
				return EINVAL;
			values[start + j] =
			    (int)((byte >> 4) * 10 + (byte & 15));
			seen |= 1U << (start + j);
		}
	}
	if (seen != 0xfffffU)
		return EINVAL;
	base[0] = values[3];
	base[1] = values[11];
	base[2] = values[19];
	for (i = 0; i < rtw8821c_txpwr_lmt_type0_tbl.count; i++) {
		if (lim[i].band != 0 || lim[i].bw != 0 ||
		    lim[i].ch != tuple->primary || lim[i].rs > 2 ||
		    (regd != 12 && lim[i].regd != regd))
			continue;
		section = lim[i].rs;
		if (lim[i].txpwr_lmt < limits[section])
			limits[section] = lim[i].txpwr_lmt;
		limit_seen |= 1U << section;
	}
	if (limit_seen != 7)
		return ENOTSUP;
	for (i = 0; i < 20; i++) {
		section = i < 4 ? 0 : i < 12 ? 1 : 2;
		value = section == 0 ? cal[group] : cal[6 + group];
		if (section) {
			diff = section == 1 ? (cal[11] & 15) : (cal[11] >> 4);
			if (diff >= 8)
				diff -= 16;
			value += diff;
		}
		offset = values[i] - base[section];
		if (limits[section] - base[section] < offset)
			offset = limits[section] - base[section];
		value += offset;
		if (value < 0)
			return ERANGE;
		if (value > 63)
			value = 63;
		result[i] = (uint8_t)value;
	}
	memcpy(out, result, sizeof(result));
	return 0;
}
#ifdef _KERNEL
#include <dev/ic/rtw88var.h>
int rtw8821c_set_tx_power_tuple(struct rtw8821c_phy *ctx,
				const struct rtw88_channel_tuple *tuple,
				unsigned regd)
{
	struct rtw88_softc *sc;
	uint8_t indices[20];
	uint32_t word;
	unsigned i;
	int error;
	if (ctx == NULL || ctx->sc == NULL)
		return EINVAL;
	sc = ctx->sc;
	error = rtw8821c_power_indices(&ctx->efuse, tuple, regd, indices);
	if (error != 0)
		return error;
	for (i = 0; i < 20; i += 4) {
		error = rtw8821c_phy_check(ctx);
		if (error != 0)
			return error;
		word = (uint32_t)indices[i] | ((uint32_t)indices[i + 1] << 8) |
		       ((uint32_t)indices[i + 2] << 16) |
		       ((uint32_t)indices[i + 3] << 24);
		error = sc->sc_ops->write_4(sc->sc_bus, 0x1d00 + i, word);
		if (error != 0)
			return error;
	}
	return 0;
}
#endif
