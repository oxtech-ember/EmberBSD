/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: FreeBSD a143197801c680b958ab662c88cf086668a1958b rtw88. */
#ifndef _DEV_IC_RTW8821C_HW_H_
#define _DEV_IC_RTW8821C_HW_H_

#include "rtw88_hw.h"

#define RTW8821C_TX_PACKET_DESC_SIZE 48
#define RTW8821C_TX_BUFFER_DESC_SIZE 16
#define RTW8821C_RX_PACKET_DESC_SIZE 24
#define RTW8821C_RX_BUFFER_DESC_SIZE 8

/* Fixed48 TX: checksum covers the first 32 bytes. Only DATA_SC=0 and
 * packet_offset=0 are supported. Write exactly 48 bytes on success and leave
 * dst unchanged on error. RX bounds descriptor, driver info and payload
 * before publishing *out; both codecs accept unaligned buffers. */
int rtw8821c_tx_encode(uint8_t *, size_t, const struct rtw88_tx_info *);
int rtw8821c_rx_decode(const uint8_t *, size_t, struct rtw88_rx_info *);

#endif /* _DEV_IC_RTW8821C_HW_H_ */
