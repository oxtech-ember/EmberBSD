#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD native table import wrapper, AI-assisted.
"""Wrap the pinned FreeBSD rtw8821c_table.c with native descriptors."""
import hashlib
import pathlib
import sys

PINNED = "ebc2547ddd66b7df434e0ef91319eddf5565c452863c4a0fd64660839d3e9a0c"
PREAMBLE = '''/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: FreeBSD a143197801c680b958ab662c88cf086668a1958b rtw8821c_table.c */

#include "rtw8821c_table.h"

typedef uint32_t u32;
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define BIT(n) (1U << (n))
#define GENMASK(h, l) (((~0U) << (l)) & (~0U >> (31U - (unsigned)(h))))
#define MASKDWORD 0xffffffffU
#define rtw_phy_cfg_mac RTW8821C_TBL_MAC
#define rtw_phy_cfg_agc RTW8821C_TBL_AGC
#define rtw_phy_cfg_bb RTW8821C_TBL_BB
#define RTW8821C_RF_PATH_A 0
#define RTW8821C_RF_PATH_B 1
#define RTW_DECL_TABLE_PHY_COND(name, cfg) \\
const struct rtw8821c_table name ## _tbl = { \\
	.data = name, .count = (uint32_t)ARRAY_SIZE(name), .kind = (cfg), \\
	.rf_path = 0 }
#define RTW_DECL_TABLE_RF_RADIO(name, path) \\
const struct rtw8821c_table name ## _tbl = { \\
	.data = name, .count = (uint32_t)ARRAY_SIZE(name), \\
	.kind = RTW8821C_TBL_RF, .rf_path = RTW8821C_RF_PATH_ ## path }
#define RTW_DECL_TABLE_BB_PG(name) \\
const struct rtw8821c_table name ## _tbl = { \\
	.data = name, .count = (uint32_t)ARRAY_SIZE(name), \\
	.kind = RTW8821C_TBL_BB_PG, .rf_path = 0 }
#define RTW_DECL_TABLE_TXPWR_LMT(name) \\
const struct rtw8821c_table name ## _tbl = { \\
	.data = name, .count = (uint32_t)ARRAY_SIZE(name), \\
	.kind = RTW8821C_TBL_TXPWR_LMT, .rf_path = 0 }
#define RTW_DECL_TABLE_DPK(name) \\
const struct rtw8821c_table name ## _tbl = { \\
	.data = name, .count = (uint32_t)ARRAY_SIZE(name), \\
	.kind = RTW8821C_TBL_DPK, .rf_path = 0 }

'''


def main():
    if len(sys.argv) != 3:
        sys.stderr.write("usage: import-rtw8821c-table.py SRC DST\n")
        return 2
    src = pathlib.Path(sys.argv[1])
    dst = pathlib.Path(sys.argv[2])
    data = src.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != PINNED:
        sys.stderr.write("unexpected source hash: %s\n" % digest)
        return 1
    text = data.decode("utf-8")
    marker = '#include "rtw8821c_table.h"\n'
    idx = text.find(marker)
    if idx < 0:
        sys.stderr.write("missing table include marker\n")
        return 1
    body = text[idx + len(marker):].replace("rtw_phy_pg_cfg_pair", "rtw8821c_pg_pair").replace("rtw_txpwr_lmt_cfg_pair", "rtw8821c_limit_pair")
    dst.write_text(PREAMBLE + body)
    return 0


if __name__ == "__main__":
    sys.exit(main())
