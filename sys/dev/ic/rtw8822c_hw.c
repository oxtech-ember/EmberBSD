/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: FreeBSD a143197801c680b958ab662c88cf086668a1958b rtw88. */

#include <sys/types.h>
#ifdef _KERNEL
#include <sys/param.h>
#include <sys/errno.h>
#else
#include <stddef.h>
#include <errno.h>
#endif

#include "rtw88_hw.h"

/* FreeBSD d2145e9b main.c/efuse.h; BSD-3-Clause option. No defaults for
 * IGNORE or unsupported encodings: unknown must remain distinguishable. */
int
rtw88_hw_feature_decode(const uint8_t *p, size_t len,
    struct rtw88_hw_feature *out)
{
	struct rtw88_hw_feature value = { 0 };
	uint32_t word;
	unsigned i;

	if (p == NULL || out == NULL || len != RTW88_HW_FEATURE_LEN)
		return EINVAL;
	for (i = 0; i < RTW88_HW_FEATURE_LEN; i++)
		value.raw[i] = p[i];
	word = (uint32_t)p[4] | (uint32_t)p[5] << 8 |
	    (uint32_t)p[6] << 16 | (uint32_t)p[7] << 24;
	value.hci = word & 15;
	value.bw = (word >> 16) & 7;
	value.nss = (word >> 19) & 3;
	value.antennas = (word >> 21) & 7;
	value.protocol = (word >> 26) & 3;
	if (value.bw == 6 || value.bw == 7) {
		value.known |= RTW88_HW_KNOWN_BW;
		value.bandwidths = value.bw == 6 ? 3 : 7;
	}
	if (value.nss == 1 || value.nss == 2)
		value.known |= RTW88_HW_KNOWN_NSS;
	if (value.antennas == 1 || value.antennas == 2)
		value.known |= RTW88_HW_KNOWN_ANTENNAS;
	if (value.protocol == 3)
		value.known |= RTW88_HW_KNOWN_PROTOCOL;
	*out = value;
	return 0;
}

/* Exact normal-firmware load profile; see rtw88-provenance.md. */
const struct rtw88_fw_profile rtw8822c_fw_profile = {
	.signature = 0x8822,
	.dmem_address = UINT32_C(0x80200000), .dmem_size = 19328,
	.imem_address = UINT32_C(0x80030000), .imem_size = 54064,
	.emem_address = UINT32_C(0x80100000), .emem_size = 128976
};

static const struct rtw88_tx_profile rtw8822c_tx_profile = {
	.checksum_bytes = 48,
	.data_sc_mask = 0x0f,
};

int
rtw88_tx_encode(uint8_t *dst, size_t cap, const struct rtw88_tx_info *tx)
{

	return rtw88_tx_encode_profile(dst, cap, tx, &rtw8822c_tx_profile);
}

int
rtw88_rx_decode(const uint8_t *src, size_t len, struct rtw88_rx_info *out)
{

	return rtw88_rx_decode_common(src, len, out);
}

/* RTL8822C query_phy_status_page0/page1 and phy.c rtw_phy_power_2_db:
 * FreeBSD d2145e9b96448da34d3cfede24c509af6d830c92, Realtek 2018-2019,
 * GPL-2.0 OR BSD-3-Clause. Native bounded byte loads and wide arithmetic. */
int
rtw88_rx_signal_decode_tuple(const uint8_t *p, size_t len,
    const struct rtw88_rx_info *rx, const struct rtw88_phy_state *ctx,
    uint32_t generation, const struct rtw88_channel_tuple *channel,
    struct rtw88_rx_signal *out)
{
    struct rtw88_rx_signal v = { 0 };
    unsigned path, vht;
    int value, selected;

    if (out == NULL)
        return EINVAL;
    v.generation = generation;
    v.length = len > UINT16_MAX ? UINT16_MAX : (uint16_t)len;
    v.page = UINT8_MAX;
    v.reason = RTW88_SIGNAL_MISSING;
    if (rx == NULL || (rx->flags & RTW88_RX_PHY_STATUS) == 0 ||
        (rx->flags & (RTW88_RX_CRC_ERROR | RTW88_RX_ICV_ERROR | RTW88_RX_C2H)) != 0)
        goto invalid;
    v.rate = rx->rate;
    vht = rx->rate >= 0x2c && rx->rate <= 0x3f;
    v.bandwidth = rx->bandwidth;
    v.reason = RTW88_SIGNAL_SHORT;
    if (p == NULL || len < 1 || len > 120)
        goto invalid;
    v.page = p[0] & 15;
    v.reason = RTW88_SIGNAL_PAGE;
    if (v.page > 1)
        goto invalid;
    v.reason = RTW88_SIGNAL_SHORT;
    if (len < (v.page == 0 ? 20U : 8U))
        goto invalid;
    v.channel = p[6];
    v.pwdb[0] = p[1];
    v.pwdb[1] = p[v.page == 0 ? 16 : 2];
    if (v.page == 0) {
        v.gain[0] = p[2] & 63;
        v.gain[1] = p[19] & 63;
    } else {
        /* Legacy OFDM uses L_RXSC; bounded native VHT uses HT_RXSC. */
        v.rxsc = vht ? (p[5] >> 4) & 15 : p[5] & 15;
    }
    v.reason = RTW88_SIGNAL_GENERATION;
    if (ctx == NULL || generation == 0 || ctx->generation != generation)
        goto invalid;
    v.paths = ctx->paths;
    v.reason = RTW88_SIGNAL_PATH;
    if (ctx->paths != 3) /* Actual native TRX AB profile only. */
        goto invalid;
    v.reason = RTW88_SIGNAL_CHANNEL;
    if (channel == NULL || channel->primary == 0 ||
        channel->primary != v.channel ||
        !((channel->primary >= 1 && channel->primary <= 13) ||
          (channel->primary >= 36 && channel->primary <= 64 &&
          channel->primary % 4 == 0) ||
          (channel->primary >= 100 && channel->primary <= 144 &&
          channel->primary % 4 == 0) ||
          (channel->primary >= 149 && channel->primary <= 165 &&
          (channel->primary - 149) % 4 == 0)))
        goto invalid;
    v.reason = RTW88_SIGNAL_RATE;
    if ((v.page == 0 && (rx->rate > 3 || channel->primary > 14)) ||
        (v.page == 1 && !((rx->rate >= 4 && rx->rate <= 11) ||
          (vht && channel->primary > 14))))
        goto invalid;
    v.reason = RTW88_SIGNAL_WIDTH;
    if (channel->width_mhz != 20 || channel->center != channel->primary ||
        rx->bandwidth != 0 || (v.page == 1 &&
        (v.rxsc > 8 || (vht && v.rxsc == 0 && !ctx->channel_valid))))
        goto invalid;
    v.reason = RTW88_SIGNAL_GAIN;
    if (v.page == 0 && (!ctx->gain_valid || ctx->gain_error != 0 ||
        ctx->gain_low > ctx->gain_high || ctx->gain_high > 63))
        goto invalid;
    v.reason = RTW88_SIGNAL_NUMBER;
    for (path = 0; path < 2; path++) {
        value = v.pwdb[path];
        if (v.page == 0) {
            if (v.gain[path] < ctx->gain_low)
                value += 2 * (ctx->gain_low - v.gain[path]);
            else if (v.gain[path] > ctx->gain_high)
                value -= 2 * (v.gain[path] - ctx->gain_high);
        }
        value -= 110;
        /* Do not reproduce signed-byte wrapping or invent a usable index. */
        if (value < -120 || value >= 20)
            goto invalid;
        v.power[path] = (int16_t)value;
    }
    selected = v.power[0]; /* CCK reference uses pathA, not max(A,B). */
    if (v.page == 1 && v.power[1] > selected)
        selected = v.power[1];
    v.quality = selected <= -100 ? 0 : selected >= 0 ? 100 :
        (uint8_t)(100 + selected);
    v.valid = 1;
    v.reason = 0;
    *out = v;
    return 0;
invalid:
    v.valid = v.quality = 0;
    *out = v;
    return ENOENT;
}

int
rtw88_rx_signal_decode(const uint8_t *p, size_t len,
    const struct rtw88_rx_info *rx, const struct rtw88_phy_state *ctx,
    uint32_t generation, unsigned channel, struct rtw88_rx_signal *out)
{
    const struct rtw88_channel_tuple tuple = { channel, channel, 20 };

    return rtw88_rx_signal_decode_tuple(p, len, rx, ctx, generation,
        &tuple, out);
}
