/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
#ifndef _DEV_IC_RTW8723D_CORE_PHY_H_
#define _DEV_IC_RTW8723D_CORE_PHY_H_
#include <dev/ic/rtw8723d_core_boot.h>
#include <dev/ic/rtw8723d.h>
#include <dev/ic/rtw8723d_phy.h>
#include <dev/ic/rtw8723d_power.h>
#include <dev/ic/rtw8723d_iqk.h>
#include <dev/ic/rtw88_profile.h>

struct rtw8723d_core_phy_config {
	struct rtw8723d_phy_condition condition;
	struct rtw8723d_efuse efuse;
	struct rtw88_profile profile;
	struct rtw8723d_power_policy power;
	unsigned channel;
};

struct rtw8723d_core_phy_result {
	struct rtw8723d_iqk_matrix iqk;
	uint32_t generation;
	unsigned channel;
	bool configured;
};

/* One sole actor, acquired F1/block owner, successful firmware boot, masked
 * interrupts and software TX closed. Identity/profile are already validated;
 * condition.package must come from the selected qualified chip profile.
 * Copy config before callbacks, preflight power before mutation, run MAC/PHY,
 * software crypto, DM/LCK/IQK and HT20 channel/power under the same generation.
 * Any stage failure retains chip ownership and marks the context failed. The
 * caller must quiesce HCI before releasing power. No live retry.
 * Result starts zeroed, remains unchanged on error, and rejects duplicate
 * successful setup in the same generation. Separate config/result/context
 * storage must stay pinned. Reset result only while the chip is fully off.
 * This does NOT perform coexistence/FW runtime setup, unpause TX, publish RF
 * readiness, start a netif, or change board power/network configuration. */
int rtw8723d_core_phy_configure(struct rtw8723d_core_boot *,
    const struct rtw8723d_core_phy_config *, struct rtw8723d_core_phy_result *);
#endif
