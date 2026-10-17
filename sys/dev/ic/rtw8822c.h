/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: FreeBSD a143197801c680b958ab662c88cf086668a1958b rtw88. */

#ifndef _DEV_IC_RTW8822C_H_
#define _DEV_IC_RTW8822C_H_

#include "rtw88_hw.h"

#define RTW8822C_EFUSE_PHYSICAL_SIZE	512
#define RTW8822C_EFUSE_PROTECTED_SIZE	124
#define RTW8822C_EFUSE_LOGICAL_SIZE	768

/* Raw calibration bytes retain erased (0xff) values; no defaults applied. */
struct rtw8822c_efuse {
	uint8_t logical[RTW8822C_EFUSE_LOGICAL_SIZE];
	uint8_t mac[6];
	uint8_t txpower[4][42];
	uint8_t channel_plan;
	uint8_t xtal;
	uint8_t board_option;
	uint8_t bt_setting;
	uint8_t rfe_option;
	uint8_t country[2];
	uint8_t thermal[2];
	uint8_t power_track_type;
};

/* PCI IDs are observed configuration-space values, not EFUSE contents. */
struct rtw8822c_identity {
	uint16_t vendor;
	uint16_t product;
	uint32_t sys_cfg1;	/* Snapshot of register 0x00f0. */
	uint8_t rfe_option;
};

/*
 * Decode a physical WIFI-bank EFUSE prefix, at most 512 bytes. Only the
 * first 388 bytes contain logical records; the protected tail is not
 * record data. A 0xff header (also an extended second header) ends records.
 * Erased logical bytes remain 0xff. Missing words/header bytes return
 * EINVAL; writes outside the 768-byte map return EOVERFLOW. Invalid MACs
 * return EINVAL. Leave *out unchanged on failure. No hardware operations.
 * Raw calibration is not a validated transmit-power or regulatory policy.
 */
int rtw8822c_efuse_decode(const uint8_t *, size_t, struct rtw8822c_efuse *);

/*
 * Accept only PCI 10ec:c822 production silicon, cut encodings A-G, and
 * evidenced RFE definitions 0-6. ENOTSUP means outside this source profile;
 * EINVAL means NULL input. This does not establish physical support.
 */
int rtw8822c_validate_identity(const struct rtw8822c_identity *);

#endif /* _DEV_IC_RTW8822C_H_ */
