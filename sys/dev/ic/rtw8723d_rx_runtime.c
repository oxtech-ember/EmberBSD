/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
/* Pure current-channel RX admission, AI-assisted; no card or stack calls. */
#ifdef _KERNEL
#include <sys/errno.h>
#else
#include <errno.h>
#endif
#include <dev/ic/rtw88var.h>
#include <dev/ic/rtw8723d_rx.h>

int
rtw8723d_rx_desc(struct rtw88_softc *sc, const uint8_t *src, size_t length,
    struct rtw88_rx_info *out)
{
	struct rtw88_rx_info value;
	struct rtw88_phy_state context;
	uint32_t generation;
	int error;

	if (sc == NULL || out == NULL)
		return EINVAL;
	error = rtw8723d_rx_decode(src, length, &value);
	if (error != 0)
		return error;
	if (value.packet_size == 0)
		return EBADMSG;
	if ((value.flags & RTW88_RX_C2H) != 0) {
		*out = value;
		return 0;
	}
	if ((value.flags & (RTW88_RX_CRC_ERROR | RTW88_RX_ICV_ERROR)) != 0) {
		*out = value;
		return 0;
	}
	context = sc->sc_phy;
	generation = sc->sc_generation;
	if (generation == 0 || context.generation != generation ||
	    !context.channel_valid)
		return EACCES;
	if (context.paths != 1 || context.channel.primary < 1 ||
	    context.channel.primary > 13 || context.channel.width_mhz != 20 ||
	    context.channel.center != context.channel.primary || value.rate > 19)
		return EOPNOTSUPP;
	value.signal.reason = RTW88_SIGNAL_MISSING;
	if ((value.flags & RTW88_RX_PHY_STATUS) != 0) {
		error = rtw8723d_rx_signal_decode(src + value.driver_info_offset,
		    value.driver_info_size, &value, &context, generation, &value.signal);
		if (error == 0)
			value.bandwidth = value.signal.bandwidth;
	}
	*out = value;
	return 0;
}
