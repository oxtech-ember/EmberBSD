/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: FreeBSD a143197801c680b958ab662c88cf086668a1958b rtw88. */
#ifndef _DEV_IC_RTW8821C_PHY_H_
#define _DEV_IC_RTW8821C_PHY_H_
#ifdef _KERNEL
#include <sys/types.h>
#else
#include <stdint.h>
#endif
#include "rtw8821c.h"
struct rtw88_softc;
struct rtw88_channel_tuple;
struct rtw8821c_phy {
	struct rtw88_softc *sc;
	struct rtw8821c_efuse efuse;
	uint32_t channel_defaults[3];
	uint8_t cck_pd_default, default_ofdm_index;
	uint8_t post_valid, srrc, associated;
	int (*command)(void *, const uint8_t *, size_t);
	void *command_cookie;
	int (*wait_ms)(void *, unsigned);
	void *wait_cookie;
	int (*check)(void *);
	void *check_cookie;
};
struct rtw8821c_phy_io {
	void *arg;
	int (*write_1)(void *, uint32_t, uint8_t);
	int (*write_4)(void *, uint32_t, uint32_t);
	void (*delay_us)(void *, unsigned);
	int (*check)(void *);
};
struct rtw8821c_phy_profile {
	unsigned cut, package, rfe;
};
/* PCIe only; validates every conditional table before its first write. */
int rtw8821c_load_tables_io(const struct rtw8821c_phy_io *,
			    const struct rtw8821c_phy_profile *);
int rtw8821c_phy_check(struct rtw8821c_phy *);
int rtw8821c_phy_pre(struct rtw8821c_phy *);
int rtw8821c_phy_post(struct rtw8821c_phy *);
int rtw8821c_calibrate(struct rtw8821c_phy *);
int rtw8821c_read_rf(struct rtw8821c_phy *, unsigned, uint32_t *);
int rtw8821c_write_rf_mask(struct rtw8821c_phy *, unsigned, uint32_t, uint32_t);
int rtw8821c_set_channel_tuple(struct rtw8821c_phy *,
			       const struct rtw88_channel_tuple *);
int rtw8821c_load_tables(struct rtw8821c_phy *);
int rtw8821c_set_tx_power_tuple(struct rtw8821c_phy *,
				const struct rtw88_channel_tuple *, unsigned);
int rtw8821c_power_indices(const struct rtw8821c_efuse *,
			   const struct rtw88_channel_tuple *, unsigned,
			   uint8_t[20]);
#endif
