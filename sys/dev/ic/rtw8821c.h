/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: FreeBSD a143197801c680b958ab662c88cf086668a1958b rtw88. */
#ifndef _DEV_IC_RTW8821C_H_
#define _DEV_IC_RTW8821C_H_
#include "rtw88_hw.h"

#define RTW8821C_EFUSE_PHYSICAL_SIZE 512
#define RTW8821C_EFUSE_PROTECTED_SIZE 96
#define RTW8821C_EFUSE_USABLE_SIZE \
    (RTW8821C_EFUSE_PHYSICAL_SIZE - RTW8821C_EFUSE_PROTECTED_SIZE)
#define RTW8821C_EFUSE_LOGICAL_SIZE 512

struct rtw8821c_efuse {
	uint8_t logical[RTW8821C_EFUSE_LOGICAL_SIZE];
	uint8_t mac[6];
	uint8_t channel_plan, xtal, thermal, pa_type;
	uint8_t lna_2g[2], lna_5g[2];
	uint8_t board_option, bt_setting, rfe_option, package_type;
	uint8_t country[2];
};

struct rtw8821c_identity {
	uint16_t vendor, product;
	uint32_t sys_cfg1;
};

/* Decode only the usable physical prefix; output is unchanged on error.
 * Calibration values are raw, not authorization for radio operation. */
int rtw8821c_efuse_decode(const uint8_t *, size_t, struct rtw8821c_efuse *);

/* Known PCI ID and production silicon only; no cut/RFE support claim. */
int rtw8821c_validate_identity(const struct rtw8821c_identity *);

extern const struct rtw88_fw_profile rtw8821c_fw_profile;
extern const struct rtw88_chip_ops rtw8821c_chip_ops;

#endif
