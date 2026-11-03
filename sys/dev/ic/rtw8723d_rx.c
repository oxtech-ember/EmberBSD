/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: protocol facts from Linux adc218676eef25575469234709c2d87185ca223a,
 * rtw88/rx.h and rtw8723d.c query_rx_desc. Native bounded loads, AI-assisted. */
#ifdef _KERNEL
#include <sys/errno.h>
#else
#include <errno.h>
#endif
#include <dev/ic/rtw8723d_rx.h>

static uint32_t
rtw8723d_rx_word(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
	    (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

int
rtw8723d_rx_decode(const uint8_t *src, size_t length, struct rtw88_rx_info *out)
{
	struct rtw88_rx_info value = { 0 };
	uint32_t w0, w2;

	if (src == NULL || out == NULL || length < RTW8723D_RX_PACKET_DESC_SIZE)
		return EINVAL;
	w0 = rtw8723d_rx_word(src);
	w2 = rtw8723d_rx_word(src + 8);
	value.packet_size = w0 & 0x3fff;
	value.driver_info_size = ((w0 >> 16) & 15) * 8;
	value.shift = (w0 >> 24) & 3;
	value.driver_info_offset = RTW8723D_RX_PACKET_DESC_SIZE + value.shift;
	value.payload_offset = value.driver_info_offset + value.driver_info_size;
	if (value.payload_offset > length ||
	    value.packet_size > length - value.payload_offset)
		return EINVAL;
	if ((w0 & (UINT32_C(1) << 14)) != 0)
		value.flags |= RTW88_RX_CRC_ERROR;
	if ((w0 & (UINT32_C(1) << 15)) != 0)
		value.flags |= RTW88_RX_ICV_ERROR;
	if ((w0 & (UINT32_C(1) << 26)) != 0)
		value.flags |= RTW88_RX_PHY_STATUS;
	if ((w0 & (UINT32_C(1) << 27)) != 0)
		value.flags |= RTW88_RX_SWDEC;
	if ((w2 & (UINT32_C(1) << 28)) != 0)
		value.flags |= RTW88_RX_C2H;
	value.security = (w0 >> 20) & 7;
	value.mac_id = rtw8723d_rx_word(src + 4) & 127;
	value.rate = rtw8723d_rx_word(src + 12) & 127;
	value.tsf_low = rtw8723d_rx_word(src + 20);
	/* Pinned 8723D query_rx_desc sets ppdu_cnt=0 and obtains BW from PHY.
	 * The zero bandwidth encoding here is not a wire measurement. */
	*out = value;
	return 0;
}
