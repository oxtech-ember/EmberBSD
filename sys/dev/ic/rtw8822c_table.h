/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: FreeBSD a143197801c680b958ab662c88cf086668a1958b rtw8822c_table. */

#ifndef _DEV_IC_RTW8822C_TABLE_H_
#define _DEV_IC_RTW8822C_TABLE_H_

#include <sys/types.h>
#ifdef _KERNEL
#include <sys/null.h>
#else
#include <stddef.h>
#include <stdint.h>
#endif

enum rtw8822c_table_kind {
	RTW8822C_TBL_MAC = 1,
	RTW8822C_TBL_AGC,
	RTW8822C_TBL_BB,
	RTW8822C_TBL_RF,
	RTW8822C_TBL_BB_PG,
	RTW8822C_TBL_TXPWR_LMT,
	RTW8822C_TBL_DPK
};

struct rtw8822c_table {
	const void *data;
	uint32_t count;
	int kind;
	int rf_path;
};

struct rtw_phy_pg_cfg_pair {
	uint32_t band;
	uint32_t rf_path;
	uint32_t tx_num;
	uint32_t addr;
	uint32_t bitmask;
	uint32_t data;
};

struct rtw_txpwr_lmt_cfg_pair {
	uint8_t regd;
	uint8_t band;
	uint8_t bw;
	uint8_t rs;
	uint8_t ch;
	int8_t txpwr_lmt;
};

extern const struct rtw8822c_table rtw8822c_mac_tbl;
extern const struct rtw8822c_table rtw8822c_agc_tbl;
extern const struct rtw8822c_table rtw8822c_bb_tbl;
extern const struct rtw8822c_table rtw8822c_bb_pg_type0_tbl;
extern const struct rtw8822c_table rtw8822c_rf_a_tbl;
extern const struct rtw8822c_table rtw8822c_rf_b_tbl;
extern const struct rtw8822c_table rtw8822c_txpwr_lmt_type0_tbl;
extern const struct rtw8822c_table rtw8822c_txpwr_lmt_type5_tbl;
extern const struct rtw8822c_table rtw8822c_dpk_afe_no_dpk_tbl;
extern const struct rtw8822c_table rtw8822c_dpk_afe_is_dpk_tbl;
extern const struct rtw8822c_table rtw8822c_dpk_mac_bb_tbl;
extern const struct rtw8822c_table rtw8822c_array_mp_cal_init_tbl;

#endif /* _DEV_IC_RTW8822C_TABLE_H_ */
