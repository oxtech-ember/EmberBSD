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

struct rtw88_softc;
/* Decode using a consistent current-generation channel snapshot. */
int rtw8723d_rx_desc(struct rtw88_softc *, const uint8_t *, size_t,
    struct rtw88_rx_info *);


/* Decode the 8723D page0/page1 path-A power report. Unlike the 8822C
 * report, no wire channel or 8822C gain correction is assumed. The caller
 * supplies the successfully programmed current-generation HT20 channel.
 * paths is the native path mask: pathA == 1, not a spatial-stream count.
 * Output always contains a reason/invalid result on failure (ENOENT), or a
 * relative 0..100 quality and vendor power convention on success. This is
 * receive metadata only, not RF calibration, rate or TX admission evidence. */
int rtw8723d_rx_signal_decode(const uint8_t *, size_t,
    const struct rtw88_rx_info *, const struct rtw88_phy_state *, uint32_t,
    struct rtw88_rx_signal *);

#endif /* _DEV_IC_RTW8723D_RX_H_ */
