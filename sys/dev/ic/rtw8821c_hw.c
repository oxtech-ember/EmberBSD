/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: FreeBSD a143197801c680b958ab662c88cf086668a1958b rtw88. */

#include "rtw8821c_hw.h"

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
