/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
#ifndef _DEV_IC_RTW8723D_COEX_H_
#define _DEV_IC_RTW8723D_COEX_H_
struct rtw8723d_runtime;
/* Exactly once after fresh legacy firmware, PHY/calibration and masked HCI
 * start, before RF/TX admission. Initial RFE0, 2.4GHz, always-awake station.
 * Antenna/BT support comes from this device's identified EFUSE, not from a
 * request to keep WLAN awake. BT-capable boards enter the documented 2G PTA
 * phase before the first scan; other boards keep the WONLY grant policy.
 * Fixed baseline tables/TDMA-off are not the complete adaptive Bluetooth
 * coexistence engine. No BT profile, power-save or throughput qualification
 * is claimed. A failure closes TX/RF and requires the existing owner unwind;
 * no same-generation retry after uncertain register/mailbox publication. */
int rtw8723d_coex_start(struct rtw8723d_runtime *);
#endif
