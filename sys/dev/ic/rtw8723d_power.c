/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright 2024 Fiona Klute. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: Linux rtw88 adc218676eef25575469234709c2d87185ca223a; native adaptation. */
/* Linux adc218676eef25575469234709c2d87185ca223a rtw88:
 * phy.c initial 2G/1SS indices/PG normalization/WW limits,
 * rtw8723d_table.c RFE0 PG and limits, rtw8723x.c TXAGC fields.
 * Native strict calibration/profile validation and wide arithmetic,
 * AI-assisted. No erased-byte defaults, thermal tracking or DPD. */
#ifdef _KERNEL
#include <sys/systm.h>
#else
#include <string.h>
#endif
#include <dev/ic/rtw8723d_power.h>
#include <dev/ic/rtw8723d.h>
#include <dev/ic/rtw88_profile.h>
#include <dev/ic/rtw88_io.h>

static const uint8_t rtw8723d_power_offset[RTW8723D_POWER_RATES] = {
	0, 0, 0, 0, 6, 6, 6, 4, 4, 2, 2, 0, 8, 6, 6, 4, 4, 2, 2, 0
};
static const uint8_t rtw8723d_power_reference[3] = { 32, 28, 26 };
static const uint8_t rtw8723d_power_world[3][13] = {
	{ 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 17 },
	{ 26, 28, 30, 30, 30, 30, 30, 30, 30, 28, 26, 24, 14 },
	{ 24, 26, 30, 30, 30, 30, 30, 30, 30, 26, 24, 23, 13 }
};

static int
rtw8723d_power_signed(unsigned value)
{
	value &= 15;
	return value >= 8 ? (int)value - 16 : (int)value;
}

int
rtw8723d_power_compute(const struct rtw8723d_efuse *efuse,
    const struct rtw88_profile *profile,
    const struct rtw8723d_power_policy *policy, unsigned channel,
    uint8_t out[RTW8723D_POWER_RATES])
{
	uint8_t result[RTW8723D_POWER_RATES];
	const uint8_t *raw;
	unsigned group, rate, section;
	int base, delta, offset, limit, total;

	if (efuse == NULL || profile == NULL || policy == NULL || out == NULL)
		return EINVAL;
	if (channel < 1 || channel > 13 || efuse->rfe != 0 ||
	    policy->rf_paths != 1 || policy->tracking_active ||
	    policy->remnant_cck != 0 || policy->remnant_ofdm != 0)
		return EOPNOTSUPP;
	if (!profile->tx_admitted || profile->domain != RTW88_REG_WORLD_MIN)
		return EACCES;
	if ((profile->channels_2g & ~UINT16_C(0x1fff)) != 0)
		return EINVAL;
	if ((profile->channels_2g & (UINT16_C(1) << (channel - 1))) == 0)
		return EACCES;
	if (policy->has_sar) {
		for (section = 0; section < 3; section++)
			if (policy->sar_offset[section] > 63)
				return EINVAL;
	}
	group = channel <= 2 ? 0 : channel <= 5 ? 1 :
	    channel <= 8 ? 2 : channel <= 11 ? 3 : 4;
	raw = efuse->txpower[0];
	if (raw[group] > 63 || raw[6 + group] > 63 || raw[11] == 0xff)
		return ENODATA;
	for (rate = 0; rate < RTW8723D_POWER_RATES; rate++) {
		section = rate < 4 ? 0 : rate < 12 ? 1 : 2;
		base = section == 0 ? raw[group] : raw[6 + group];
		delta = section == 0 ? 0 : rtw8723d_power_signed(
		    section == 1 ? raw[11] : raw[11] >> 4);
		offset = rtw8723d_power_offset[rate];
		limit = (int)rtw8723d_power_world[section][channel - 1] -
		    rtw8723d_power_reference[section];
		if (offset > limit)
			offset = limit;
		/* SAR_NONE uses max63 in the donor and cannot raise the WW cap. */
		if (policy->has_sar && offset > policy->sar_offset[section])
			offset = policy->sar_offset[section];
		total = base + delta + offset; /* Zero startup tracking remnant. */
		/* A limit below the minimum programmable index cannot be met by
		 * rounding upward to zero. Reject the complete table instead. */
		if (total < 0)
			return ERANGE;
		result[rate] = total > 63 ? 63 : total;
	}
	memcpy(out, result, sizeof(result));
	return 0;
}

int
rtw8723d_power_program(struct rtw88_io *io,
    const struct rtw8723d_efuse *efuse, const struct rtw88_profile *profile,
    const struct rtw8723d_power_policy *policy, unsigned channel,
    int (*check)(void *), void *cookie)
{
	struct rtw88_io validation;
	uint8_t indices[RTW8723D_POWER_RATES];
	uint32_t address, mask;
	unsigned rate;
	int error;

	if (io == NULL)
		return EINVAL;
	if (io->error != 0)
		return io->error;
	error = rtw8723d_power_compute(efuse, profile, policy, channel, indices);
	if (error != 0)
		return io->error = error;
	rtw88_io_init(&validation, io->ops, io->cookie);
	if (validation.error != 0)
		return io->error = validation.error;
	for (rate = 0; rate < RTW8723D_POWER_RATES; rate++) {
		if (check != NULL)
			io->error = check(cookie);
		if (io->error != 0)
			return io->error;
		if (rate == 0) {
			address = 0xe08;
			mask = 0x0000ff00;
		} else if (rate < 4) {
			address = 0x86c;
			mask = UINT32_C(0xff) << (rate * 8);
		} else {
			address = rate < 8 ? 0xe00 : rate < 12 ? 0xe04 :
			    rate < 16 ? 0xe10 : 0xe14;
			mask = UINT32_C(0xff) << ((rate & 3) * 8);
		}
		rtw88_io_wr_mask(io, address, mask, indices[rate]);
		if (io->error != 0)
			return io->error;
	}
	if (check != NULL)
		io->error = check(cookie);
	return io->error;
}
