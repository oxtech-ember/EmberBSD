/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: Linux adc218676eef25575469234709c2d87185ca223a rtw88/phy.c;
 * native validation and fallible table application are AI-assisted. */
#ifndef _DEV_IC_RTW8723D_PHY_H_
#define _DEV_IC_RTW8723D_PHY_H_

#include <dev/ic/rtw8723d_tables.h>

struct rtw88_io;

/* Table-condition selectors, not transport registration or device discovery. */
#define RTW8723D_PHY_PCIE 1U
#define RTW8723D_PHY_USB 2U
#define RTW8723D_PHY_SDIO 4U

struct rtw8723d_phy_condition {
	uint8_t cut, package, interface, rfe;
};

/* Inputs come from identified chip, efuse and HCI state. The caller owns a
 * powered, serialized chip and unwinds partial writes on error. Images must
 * remain immutable throughout the call. The condition is copied on entry.
 * The optional check returns an errno to cancel before the next pair.
 * This stage does not establish calibration, power limits or TX readiness. */
int rtw8723d_phy_apply_table(struct rtw88_io *,
    const struct rtw8723d_phy_condition *, const struct rtw8723d_phy_image *,
    int (*)(void *), void *);

/* Apply the pinned MAC, BB, AGC and RF-A images in donor order. Validate all
 * four images before the first callback or scalar access. PG/power-limit
 * data and calibration remain separate required startup stages. */
int rtw8723d_phy_load_tables(struct rtw88_io *,
    const struct rtw8723d_phy_condition *, int (*)(void *), void *);

/* Enable the BB/RF domain, load the four images, and apply the pinned chip's
 * scalar post-table setup. crystal_cap is the decoded 6-bit efuse value; afe
 * is the decoded efuse byte. MAC power/firmware and rtw8723d_mac_init precede
 * this call. Software TX must remain closed. On failure the caller unwinds
 * chip power; success still requires DM setup, LCK/IQK, channel/power limits
 * and coexistence setup before runtime admission. No board power is changed. */
int rtw8723d_phy_configure(struct rtw88_io *,
    const struct rtw8723d_phy_condition *, uint8_t, uint8_t,
    int (*)(void *), void *);

#endif /* _DEV_IC_RTW8723D_PHY_H_ */
