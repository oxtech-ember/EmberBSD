/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD; AI-assisted RTL8723D receive-signal contracts. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <dev/ic/rtw8723d_rx.h>

int
main(void)
{
	uint8_t phy[28] = {0};
	struct rtw88_rx_info rx = {0};
	struct rtw88_phy_state ctx = {0};
	struct rtw88_rx_signal out;

	rx.flags = RTW88_RX_PHY_STATUS;
	rx.rate = 3;
	ctx.generation = 7;
	ctx.paths = 1;
	ctx.channel_valid = 1;
	ctx.channel.primary = ctx.channel.center = 6;
	ctx.channel.width_mhz = 20;
	phy[1] = 80;
	assert(rtw8723d_rx_signal_decode(phy, 4, &rx, &ctx, 7, &out) == 0);
	assert(out.valid == 1 && out.generation == 7 && out.page == 0 &&
	    out.power[0] == -17 && out.quality == 83 && out.channel == 6);
	assert(rtw8723d_rx_signal_decode(phy, 3, &rx, &ctx, 7, &out) == ENOENT);
	assert(!out.valid && out.reason == RTW88_SIGNAL_SHORT);
	assert(rtw8723d_rx_signal_decode(phy, 4, &rx, &ctx, 8, &out) == ENOENT);
	assert(!out.valid && out.reason == RTW88_SIGNAL_GENERATION);
	ctx.channel.width_mhz = 40;
	assert(rtw8723d_rx_signal_decode(phy, 4, &rx, &ctx, 7, &out) == ENOENT);
	assert(!out.valid && out.reason == RTW88_SIGNAL_WIDTH);
	ctx.channel.width_mhz = 20; rx.flags |= RTW88_RX_C2H;
	assert(rtw8723d_rx_signal_decode(phy, 4, &rx, &ctx, 7, &out) == ENOENT);
	assert(!out.valid && out.reason == RTW88_SIGNAL_MISSING);
	rx.flags = RTW88_RX_PHY_STATUS; rx.rate = 4; phy[0] = 1; phy[1] = 90;
	assert(rtw8723d_rx_signal_decode(phy, sizeof(phy), &rx, &ctx, 7, &out) == 0);
	assert(out.valid && out.page == 1 && out.power[0] == -20 && out.quality == 80);
	assert(rtw8723d_rx_signal_decode(phy, sizeof(phy), &rx, &ctx, 7, NULL) == EINVAL);
	puts("PASS: RTL8723D PHY signal pages, generation, bounds and refusal reasons");
	return 0;
}
