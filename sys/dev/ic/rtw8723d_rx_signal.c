/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: register/report facts from Linux adc218676eef25575469234709c2d87185ca223a,
 * rtw88/rtw8723d.c query_phy_status_page0/page1 and rtw8723d.h.
 * Native bounded loads and explicit cached-channel validation, AI-assisted. */
#ifdef _KERNEL
#include <sys/errno.h>
#else
#include <errno.h>
#endif
#include <dev/ic/rtw8723d_rx.h>

int
rtw8723d_rx_signal_decode(const uint8_t *p, size_t length,
    const struct rtw88_rx_info *rx, const struct rtw88_phy_state *ctx,
    uint32_t generation, struct rtw88_rx_signal *out)
{
	struct rtw88_rx_signal value = { 0 };
	unsigned rf_mode;
	int power;

	if (out == NULL)
		return EINVAL;
	value.generation = generation;
	value.length = length > UINT16_MAX ? UINT16_MAX : (uint16_t)length;
	value.page = UINT8_MAX;
	value.reason = RTW88_SIGNAL_MISSING;
	if (rx == NULL || (rx->flags & RTW88_RX_PHY_STATUS) == 0 ||
	    (rx->flags & (RTW88_RX_CRC_ERROR | RTW88_RX_ICV_ERROR | RTW88_RX_C2H)) != 0)
		goto invalid;
	value.rate = rx->rate;
	value.reason = RTW88_SIGNAL_SHORT;
	if (p == NULL || length < 1 || length > 120)
		goto invalid;
	value.page = p[0] & 15;
	value.reason = RTW88_SIGNAL_PAGE;
	if (value.page > 1)
		goto invalid;
	value.reason = RTW88_SIGNAL_SHORT;
	/* Page1 includes EVM/CFO/SNR through byte24 in the pinned report. */
	if (length < (value.page == 0 ? 4U : 28U))
		goto invalid;
	value.reason = RTW88_SIGNAL_GENERATION;
	if (ctx == NULL || generation == 0 || ctx->generation != generation)
		goto invalid;
	value.paths = ctx->paths;
	value.reason = RTW88_SIGNAL_PATH;
	if (ctx->paths != 1)
		goto invalid;
	value.reason = RTW88_SIGNAL_CHANNEL;
	if (!ctx->channel_valid || ctx->channel.primary < 1 ||
	    ctx->channel.primary > 13)
		goto invalid;
	value.channel = (uint8_t)ctx->channel.primary;
	value.reason = RTW88_SIGNAL_WIDTH;
	if (ctx->channel.width_mhz != 20 ||
	    ctx->channel.center != ctx->channel.primary)
		goto invalid;
	value.reason = RTW88_SIGNAL_RATE;
	if ((value.page == 0 && rx->rate > 3) ||
	    (value.page == 1 && (rx->rate < 4 || rx->rate > 19)))
		goto invalid;
	value.pwdb[0] = p[1];
	if (value.page == 1) {
		/* OFDM uses L_RXSC; HT MCS0..7 uses HT_RXSC. */
		value.rxsc = rx->rate < 12 ? p[5] & 15 : (p[5] >> 4) & 15;
		rf_mode = (p[15] >> 4) & 3;
		value.reason = RTW88_SIGNAL_WIDTH;
		if (rf_mode > 1 || (rf_mode != 0 &&
		    value.rxsc != 1 && value.rxsc != 2))
			goto invalid;
	}
	/* Native bandwidth encoding zero means20MHz. A 40MHz report is rejected
	 * above; it must not be relabelled to fit the initial channel envelope. */
	value.bandwidth = 0;
	power = (int)value.pwdb[0] - (value.page == 0 ? 97 : 110);
	value.reason = RTW88_SIGNAL_NUMBER;
	if (power < -120 || power >= 20)
		goto invalid;
	value.power[0] = (int16_t)power;
	/* Native relative stack index, not the donor's DM linear-power RSSI. */
	value.quality = power <= -100 ? 0 : power >= 0 ? 100 : (uint8_t)(100 + power);
	value.valid = 1;
	value.reason = 0;
	*out = value;
	return 0;
invalid:
	value.valid = value.quality = 0;
	*out = value;
	return ENOENT;
}
