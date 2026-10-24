/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: FreeBSD a143197801c680b958ab662c88cf086668a1958b rtw88 TX power. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <dev/ic/rtw88var.h>
#include <dev/ic/rtw8822c.h>
#include <dev/ic/rtw8822c_table.h>

static const unsigned rate_first[6] = { 0, 4, 12, 20, 44, 54 };
static const unsigned rate_count[6] = { 4, 8, 8, 8, 10, 10 };
static const unsigned rate_base[6] = { 3, 11, 19, 27, 51, 61 };

static int
signed_nibble(unsigned value)
{

	value &= 15;
	return value < 8 ? (int)value : (int)value - 16;
}

static int
channel_group(unsigned ch)
{
	static const unsigned first[] = {
		36, 44, 52, 60, 100, 108, 116, 124, 132, 140,
		149, 157, 165, 173
	};
	unsigned i;

	if (ch >= 1 && ch <= 14)
		return ch <= 2 ? 0 : ch <= 5 ? 1 : ch <= 8 ? 2 :
		    ch <= 11 ? 3 : 4;
	for (i = 0; i < __arraycount(first); i++) {
		if (ch >= first[i] && ch <= first[i] + 4 &&
		    (ch - first[i]) % 4 == 0 &&
		    !(ch == 68 || ch == 148 || ch == 169))
			return (int)i;
	}
	return -1;
}

/* Decode only the two-path, <=2SS PG format present in the pinned table. */
static int
power_by_rate(unsigned band, int values[2][64], int bases[2][6])
{
	const struct rtw_phy_pg_cfg_pair *rows = rtw8822c_bb_pg_type0_tbl.data;
	uint64_t seen[2] = { 0, 0 };
	unsigned i, j, section, start, offset;

	memset(values, 0, 2 * 64 * sizeof(values[0][0]));
	for (i = 0; i < rtw8822c_bb_pg_type0_tbl.count; i++) {
		const struct rtw_phy_pg_cfg_pair *row = &rows[i];

		if (row->band != band)
			continue;
		if (row->rf_path > 1 || row->bitmask != UINT32_MAX)
			return EINVAL;
		offset = row->addr - (row->rf_path ? 0xe20 : 0xc20);
		if (offset > 0x2c || (offset & 3) != 0)
			return EINVAL;
		start = offset < 0x1c ? offset : offset + 16;
		for (j = 0; j < 4; j++) {
			values[row->rf_path][start + j] =
			    (int)((row->data >> (8 * j)) & 0xff);
			seen[row->rf_path] |= UINT64_C(1) << (start + j);
		}
	}
	for (i = 0; i < 2; i++) {
		for (section = band == 0 ? 0 : 1; section < 6; section++) {
			bases[i][section] = values[i][rate_base[section]];
			for (j = 0; j < rate_count[section]; j++) {
				start = rate_first[section] + j;
				if ((seen[i] & (UINT64_C(1) << start)) == 0)
					return EINVAL;
				values[i][start] -= bases[i][section];
			}
		}
		if (band != 0)
			bases[i][0] = 0;
	}
	return 0;
}

static int
power_limits(unsigned band, unsigned channel, unsigned bw, unsigned rfe,
	unsigned regd, const int bases[2][6], unsigned required,
	int limits[6])
{
	const struct rtw8822c_table *table = rfe == 5 ?
	    &rtw8822c_txpwr_lmt_type5_tbl : &rtw8822c_txpwr_lmt_type0_tbl;
	const struct rtw_txpwr_lmt_cfg_pair *rows = table->data;
	int world[6] = { 127, 127, 127, 127, 127, 127 };
	unsigned i, rs, selected = 0, world_seen = 0, seen;
	int limit, configured = 0;

	for (rs = 0; rs < 6; rs++)
		limits[rs] = 127;
	for (i = 0; i < table->count; i++) {
		const struct rtw_txpwr_lmt_cfg_pair *row = &rows[i];

		if (row->regd == regd)
			configured = 1;
		if (row->band != band || row->bw != bw ||
		    row->ch != channel || row->rs >= 6)
			continue;
		rs = row->rs;
		limit = row->txpwr_lmt < -127 ? -127 : row->txpwr_lmt;
		if (row->regd == regd) {
			limits[rs] = limit;
			selected |= 1U << rs;
		}
		world_seen |= 1U << rs;
		if (limit < world[rs])
			world[rs] = limit;
	}
	if (regd == 12 || !configured) {
		memcpy(limits, world, sizeof(world));
		seen = world_seen;
	} else
		seen = selected;
	/* The reference cross-fills missing HT/VHT limits for 5 GHz. */
	if (band != 0) {
		for (rs = 2; rs <= 3; rs++) {
			if (limits[rs] == 127 && (seen & (1U << (rs + 2)))) {
				limits[rs] = limits[rs + 2];
				seen |= 1U << rs;
			} else if (limits[rs + 2] == 127 && (seen & (1U << rs))) {
				limits[rs + 2] = limits[rs];
				seen |= 1U << (rs + 2);
			}
		}
	} else {
		/*
		 * Deliberate safety change: upstream has no 2G VHT limit rows.
		 * Bound those unused rates by the absolute HT limits before
		 * their own by-rate base conversion. Never use default 127.
		 */
		for (rs = 4; rs <= 5; rs++) {
			if (!(seen & (1U << rs)) && (seen & (1U << (rs - 2)))) {
				limits[rs] = limits[rs - 2];
				seen |= 1U << rs;
			}
		}
	}
	if ((seen & required) != required)
		return ENOTSUP;
	for (rs = 0; rs < 6; rs++)
		limits[rs] -= bases[0][rs];
	return 0;
}

/* All arithmetic is signed and checked before any hardware access. */
static int
power_indices(const struct rtw8822c_efuse *efuse,
    const struct rtw88_channel_tuple *tuple,
    const struct rtw8822c_channel_geometry *geometry, unsigned regd,
    uint8_t indices[2][64])
{
	int values[2][64], bases[2][6], limits[6];
	int group = channel_group(tuple->primary), upper_group;
	unsigned band = tuple->primary > 14, path, rs, j;
	int error;

	if ((error = power_by_rate(band, values, bases)) != 0)
		return error;
	if (tuple->width_mhz == 80) {
		int lower_group = channel_group(tuple->center - 6);

		upper_group = channel_group(tuple->center + 2);
		if (lower_group < 0 || upper_group != lower_group + 1 ||
		    upper_group >= 14)
			return ENOTSUP;
		group = lower_group;
	} else
		upper_group = group;
	if (tuple->width_mhz == 20) {
		if ((error = power_limits(band, tuple->primary, 0,
		    efuse->rfe_option, regd, bases, band ? 0x3eU : 0x3fU,
		    limits)) != 0)
			return error;
	} else {
		int lim20[6], lim40[6], lim80[6];

		if ((error = power_limits(band, tuple->primary, 0,
		    efuse->rfe_option, regd, bases, band ? 0x3eU : 0x3fU,
		    lim20)) != 0 ||
		    (error = power_limits(band, geometry->cch40, 1,
		    efuse->rfe_option, regd, bases, 0x3cU, lim40)) != 0)
			return error;
		if (tuple->width_mhz == 80 &&
		    (error = power_limits(band, tuple->center, 2,
		    efuse->rfe_option, regd, bases, 0x30U, lim80)) != 0)
			return error;
		for (rs = 0; rs < 6; rs++) {
			limits[rs] = lim20[rs];
			if (rs >= 2 && lim40[rs] < limits[rs])
				limits[rs] = lim40[rs];
			if (rs >= 4 && tuple->width_mhz == 80 &&
			    lim80[rs] < limits[rs])
				limits[rs] = lim80[rs];
		}
	}
	memset(indices, 0, 2 * 64 * sizeof(indices[0][0]));
	for (path = 0; path < 2; path++) {
		const uint8_t *cal = efuse->txpower[path];

		for (rs = band == 0 ? 0 : 1; rs < 6; rs++) {
			unsigned base_offset;
			int base;

			base_offset = band != 0 ? 18 + (unsigned)group :
			    rs == 0 ? (tuple->primary == 14 ? 5 : (unsigned)group) :
			    6 + (unsigned)group;
			if (cal[base_offset] == 0xff ||
			    (tuple->width_mhz == 80 && rs >= 2 &&
			    cal[18 + (unsigned)upper_group] == 0xff))
				return EINVAL;
			base = cal[base_offset];
			if (tuple->width_mhz == 80 && rs >= 2)
				base = (base + cal[18 + (unsigned)upper_group]) / 2;
			if (base > 127)
				return ERANGE;
			if (rs != 0) {
				unsigned diff = band != 0 ? 32 : 11;
				int delta = 0;

				if (tuple->width_mhz == 20 || rs == 1) {
					if (cal[diff] == 0xff)
						return EINVAL;
					delta = signed_nibble(cal[diff] >>
					    (rs == 1 ? 0 : 4));
				}
				if (tuple->width_mhz == 20 && (rs == 3 || rs == 5)) {
					if (cal[band != 0 ? 33 : diff + 1] == 0xff)
						return EINVAL;
					delta += signed_nibble(cal[band != 0 ? 33 : diff + 1]);
				} else if (tuple->width_mhz == 40 && band != 0 &&
				    (rs == 3 || rs == 5)) {
					if (cal[33] == 0xff)
						return EINVAL;
					delta = signed_nibble(cal[33] >> 4);
				} else if (tuple->width_mhz == 80 && band != 0 &&
				    rs >= 2) {
					if (cal[38] == 0xff)
						return EINVAL;
					delta = signed_nibble(cal[38] >> 4);
					if (rs == 3 || rs == 5) {
						if (cal[39] == 0xff)
							return EINVAL;
						delta += signed_nibble(cal[39] >> 4);
					}
				}
				base += 2 * delta;
				if (base < 0 || base > 127)
					return ERANGE;
			}
			for (j = 0; j < rate_count[rs]; j++) {
				unsigned rate = rate_first[rs] + j;
				int offset = values[path][rate];
				int result;

				if (limits[rs] < offset)
					offset = limits[rs];
				/* DIS_DPD_RATEALL: -6 dB at two TXGI steps/dB. */
				if (rs != 0 && j < 2)
					offset -= 12;
				result = base + offset;
				if (result < 0 || result > 127)
					return ERANGE;
				indices[path][rate] = (uint8_t)result;
			}
		}
	}
	return 0;
}

static int
write_mask(struct rtw88_softc *sc, uint32_t addr, uint32_t mask,
    uint32_t value)
{
	uint32_t before;
	int error;

	if (sc->sc_stop_requested)
		return EINTR;
	if ((error = sc->sc_ops->read_4(sc->sc_bus, addr, &before)) != 0)
		return error;
	if (sc->sc_stop_requested)
		return EINTR;
	return sc->sc_ops->write_4(sc->sc_bus, addr,
	    (before & ~mask) | ((value << __builtin_ctz(mask)) & mask));
}

static int
write_power(struct rtw88_softc *sc, uint32_t addr, uint32_t mask,
    uint32_t value)
{
	int error;

	if ((error = write_mask(sc, 0x1c90, 0x8000, 0)) != 0)
		return error;
	return write_mask(sc, addr, mask, value);
}

int
rtw8822c_set_tx_power_tuple(struct rtw88_softc *sc,
    const struct rtw8822c_efuse *efuse,
    const struct rtw88_channel_tuple *tuple, unsigned regd)
{
	uint8_t indices[2][64];
	struct rtw8822c_channel_geometry geometry;
	uint32_t diff_words[16] = { 0 };
	unsigned path, rs, j, rate;
	int error;

#ifdef RTW88_RFK_EXPERIMENT
	if (sc != NULL) {
		sc->sc_operating.prepared = sc->sc_operating.sealed = 0;
		sc->sc_power_program.valid = 0;
		if (sc->sc_power_program.serial == UINT64_MAX)
			return EOVERFLOW;
		sc->sc_power_program.serial++;
	}
#endif
	if (sc == NULL || efuse == NULL || tuple == NULL || sc->sc_ops == NULL ||
	    sc->sc_ops->read_4 == NULL || sc->sc_ops->write_4 == NULL ||
	    sc->sc_rfe != efuse->rfe_option)
		return EINVAL;
	if ((error = rtw8822c_channel_geometry(tuple, &geometry)) != 0)
		return error;
	if (tuple->width_mhz != 20 && tuple->primary <= 14)
		return EOPNOTSUPP;
	if (channel_group(tuple->primary) < 0)
		return ENOTSUP;
	if (regd != 2 && regd != 12)
		return ENOTSUP;
	if (sc->sc_rfe > 6)
		return ENOTSUP;
	if (sc->sc_stop_requested)
		return EINTR;
	if ((error = power_indices(efuse, tuple, &geometry, regd, indices)) != 0)
		return error;
	/* The shared TXAGC differences are signed seven-bit hardware fields. */
	for (rs = 0; rs < 6; rs++) {
		unsigned ref_rate = rs == 0 ? 3 : 19;

		for (j = 0; j < rate_count[rs]; j++) {
			int diff_a, diff_b, diff;

			rate = rate_first[rs] + j;
			diff_a = (int)indices[0][rate] - indices[0][ref_rate];
			diff_b = (int)indices[1][rate] - indices[1][ref_rate];
			diff = diff_a < diff_b ? diff_a : diff_b;
			if (diff < -64 || diff > 63)
				return ERANGE;
			diff_words[rate / 4] |=
			    ((uint32_t)diff & 0x7f) << ((rate % 4) * 8);
		}
	}
	sc->sc_phy.channel_valid = 0;
	for (path = 0; path < 2; path++) {
		error = write_power(sc, path ? 0x41a0 : 0x18a0, 0x7f0000,
		    indices[path][3]);
		if (error != 0)
			return error;
	}
	for (path = 0; path < 2; path++) {
		error = write_power(sc, path ? 0x41e8 : 0x18e8, 0x1fc00,
		    indices[path][19]);
		if (error != 0)
			return error;
	}
	for (rate = 0; rate < 64; rate += 4) {
		if (rate >= 28 && rate < 44)
			continue;
		error = write_power(sc, 0x3a00 + rate, UINT32_MAX,
		    diff_words[rate / 4]);
		if (error != 0)
			return error;
	}
#ifdef RTW88_RFK_EXPERIMENT
	if (tuple->width_mhz == 20) {
		/* This evidence qualifies only the existing HT20 RFK consumer. */
		sc->sc_power_program.generation = sc->sc_generation;
		sc->sc_power_program.channel = tuple->primary;
		sc->sc_power_program.regd = regd;
		sc->sc_power_program.cut = sc->sc_cut;
		sc->sc_power_program.rfe = sc->sc_rfe;
		sc->sc_power_program.ht_mcs = 0xff;
		for (path = 0; path < 2; path++) {
			sc->sc_power_program.ht_ref[path] = indices[path][19];
			sc->sc_power_program.ht_diff[path] = diff_words[3 + path];
		}
		sc->sc_power_program.valid = 1;
	}
#endif
	return 0;
}

int
rtw8822c_set_tx_power(struct rtw88_softc *sc,
    const struct rtw8822c_efuse *efuse, unsigned channel, unsigned regd)
{
	const struct rtw88_channel_tuple tuple = { channel, channel, 20 };

	return rtw8822c_set_tx_power_tuple(sc, efuse, &tuple, regd);
}
