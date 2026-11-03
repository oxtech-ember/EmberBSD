/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: Linux adc218676eef25575469234709c2d87185ca223a
 * rtw88/rtw8723d_table.c; native views are AI-assisted. */
#ifndef _DEV_IC_RTW8723D_TABLES_H_
#define _DEV_IC_RTW8723D_TABLES_H_
#ifdef _KERNEL
#include <sys/types.h>
#else
#include <stddef.h>
#include <stdint.h>
#endif

enum rtw8723d_phy_space {
	RTW8723D_PHY_MAC = 1,
	RTW8723D_PHY_AGC,
	RTW8723D_PHY_BB,
	RTW8723D_PHY_RF
};

/* Raw initialization pairs only. PG/power-limit records have another format.
 * Loading these images does not establish valid calibration or TX power. */
struct rtw8723d_phy_image {
	const uint32_t *data;
	size_t count;
	enum rtw8723d_phy_space space;
	unsigned rf_path;
};

extern const struct rtw8723d_phy_image rtw8723d_phy_mac;
extern const struct rtw8723d_phy_image rtw8723d_phy_agc;
extern const struct rtw8723d_phy_image rtw8723d_phy_bb;
extern const struct rtw8723d_phy_image rtw8723d_phy_rf_a;

#endif /* _DEV_IC_RTW8723D_TABLES_H_ */
