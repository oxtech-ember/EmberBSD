/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD; AI-assisted native RTL8723D descriptor interface. */
#ifndef _DEV_IC_RTW8723D_RX_H_
#define _DEV_IC_RTW8723D_RX_H_
#include <dev/ic/rtw88_hw.h>

#define RTW8723D_RX_PACKET_DESC_SIZE 24U
/* Bounds-only 24-byte wire decode. PPDU is zero for this chip; bandwidth
 * is not read from descriptor word4. C2H does not consume PHY metadata.
 * Host sequence/generation are left zero. Output unchanged on error. */
int rtw8723d_rx_decode(const uint8_t *, size_t, struct rtw88_rx_info *);


#endif /* _DEV_IC_RTW8723D_RX_H_ */
