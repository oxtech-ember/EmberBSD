/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: FreeBSD a143197801c680b958ab662c88cf086668a1958b rtw88. */

#include "rtw8821c_hw.h"
#ifdef _KERNEL
#include <sys/errno.h>
#else
#include <errno.h>
#endif

static const struct rtw88_tx_profile rtw8821c_tx_profile = {
	.checksum_bytes = 32,
	.data_sc_mask = 0,
};

int
rtw8821c_tx_encode(uint8_t *dst, size_t cap, const struct rtw88_tx_info *tx)
{

	return rtw88_tx_encode_profile(dst, cap, tx, &rtw8821c_tx_profile);
}

int
rtw8821c_rx_decode(const uint8_t *src, size_t len, struct rtw88_rx_info *out)
{

	return rtw88_rx_decode_common(src, len, out);
}

/* FreeBSD rtw88/rtw8821c.c at a143197801c680b958ab662c88cf086668a1958b:
 * Copyright(c) 2018-2019 Realtek Corporation; GPL-2.0 OR BSD-3-Clause.
 * Single-path CCK gain tables and OFDM PWDB; bounded optional metadata.
 * Channel byte layout: Realtek PHYDM phydm_phystatus.h at vendor commit
 * 2c1684a8839df13b70d13f609528218eef95c28e; no vendor code copied. */
int
rtw8821c_signal_decode(const uint8_t *p, size_t len,
    const struct rtw88_rx_info *rx, unsigned rfe, uint32_t generation,
    uint32_t current, const struct rtw88_channel_tuple *channel, int valid,
    struct rtw88_rx_signal *out)
{
	static const int8_t gain0[] = { 22, 8, -6, -22, -31, -40, -46, -52 };
	static const int8_t gain1[] = { 10, 6, 2, -2, -6, -10, -14, -17,
	    -20, -24, -28, -31, -34, -37, -40, -44 };
	struct rtw88_rx_signal v = { 0 };
	unsigned lna, vga;
	int power;

	if (out == NULL)
		return EINVAL;
	v.generation = generation;
	v.length = len > UINT16_MAX ? UINT16_MAX : (uint16_t)len;
	v.page = UINT8_MAX;
	v.reason = RTW88_SIGNAL_MISSING;
	if (rx == NULL || !(rx->flags & RTW88_RX_PHY_STATUS) ||
	    (rx->flags & (RTW88_RX_C2H | RTW88_RX_CRC_ERROR |
	    RTW88_RX_ICV_ERROR)))
		goto invalid;
	v.rate = rx->rate;
	v.reason = RTW88_SIGNAL_SHORT;
	if (p == NULL || len < 1 || len > 120)
		goto invalid;
	v.page = p[0] & 15;
	v.reason = RTW88_SIGNAL_PAGE;
	if (v.page > 1)
		goto invalid;
	v.reason = RTW88_SIGNAL_SHORT;
	if (len < (v.page == 0 ? 15U : 7U))
		goto invalid;
	v.reason = RTW88_SIGNAL_GENERATION;
	if (!generation || generation != current || !valid)
		goto invalid;
	v.channel = p[6];
	v.reason = RTW88_SIGNAL_CHANNEL;
	if (channel == NULL || channel->primary < 1 || channel->primary > 13 ||
	    channel->primary != v.channel || channel->center != channel->primary ||
	    channel->width_mhz != 20)
		goto invalid;
	v.reason = RTW88_SIGNAL_WIDTH;
	if (rx->bandwidth != 0)
		goto invalid;
	v.reason = RTW88_SIGNAL_RATE;
	if ((v.page == 0 && rx->rate > 3) ||
	    (v.page == 1 && (rx->rate < 4 || rx->rate > 11)))
		goto invalid;
	v.reason = RTW88_SIGNAL_PATH;
	if (rfe != 0 && rfe != 2 && rfe != 4 && rfe != 6)
		goto invalid;
	if (v.page == 0) {
		lna = (p[13] >> 5) | ((p[14] >> 7) << 3);
		vga = p[13] & 31;
		v.reason = RTW88_SIGNAL_GAIN;
		if (rfe == 0 && lna >= sizeof(gain0))
			goto invalid;
		power = (rfe == 0 ? gain0[lna] : gain1[lna]) - 2 * (int)vga;
		v.gain[0] = (uint8_t)lna;
	} else {
		v.pwdb[0] = p[1];
		v.rxsc = p[5] & 15;
		v.reason = RTW88_SIGNAL_WIDTH;
		if (rx->bandwidth != 0 || v.rxsc > 8)
			goto invalid;
		power = (int)p[1] - 110;
	}
	v.reason = RTW88_SIGNAL_NUMBER;
	if (power < -120 || power >= 20)
		goto invalid;
	v.power[0] = power;
	v.paths = 1;
	v.quality = power <= -100 ? 0 : power >= 0 ? 100 : 100 + power;
	v.valid = 1;
	v.reason = 0;
	*out = v;
	return 0;
invalid:
	*out = v;
	return ENOENT;
}
