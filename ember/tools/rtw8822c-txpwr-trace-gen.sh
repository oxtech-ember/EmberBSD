#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD RTL8822C TX power reference trace generator, AI-assisted.
#
# Regenerate rtw8822c-txpwr.trace: the pinned FreeBSD rtw88 TX power
# code (rtw_phy_init_tx_power, bb_pg and txpwr_lmt parsing, by-rate and
# limit config, rtw_phy_set_tx_power_level, rtw8822c_set_tx_power_index)
# runs unchanged on the EmberBSD copy of the RTL8822C tables and a fixed
# EFUSE TX power image, against the recording register model.
# Usage: rtw8822c-txpwr-trace-gen.sh REFDIR > rtw8822c-txpwr.trace
set -eu
[ "$#" -eq 1 ] || { echo "usage: $0 REFDIR" >&2; exit 2; }
ref=$1
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/rtw8822c-txpwr-trace.XXXXXXXX")
# RTW_TRACE_KEEP=DIR keeps the extracted text for inspection.
trap '[ -z "${RTW_TRACE_KEEP:-}" ] || cp -R "$work"/. "$RTW_TRACE_KEEP"; rm -rf "$work"' \
    EXIT HUP INT TERM

check() {
	sum=$(shasum -a 256 "$ref/$1" | cut -d' ' -f1)
	[ "$sum" = "$2" ] || { echo "$1: SHA256 $sum, want $2" >&2; exit 1; }
}
check rtw8822c.c 6986e6107da9261b97277d57fcc11daf5dcb0d71571c55b1080bdea1495ce160
check reg.h c8246f9305539347fdb62b0294fe12ea24769cb3ad56c2b0235c4269a0307324

x="python3 $tools/rtw88-ref-extract.py $ref"
$x "$work/defs.h" main.h:defines phy.h:defines rtw8822c.h:defines regd.c:defines \
    main.h:enums phy.h:enums phy.c:type:rtw_phy_band_type
$x "$work/types.h" \
    main.h:type:rtw_2g_1s_pwr_idx_diff main.h:type:rtw_2g_ns_pwr_idx_diff \
    main.h:type:rtw_2g_txpwr_idx main.h:type:rtw_5g_ht_1s_pwr_idx_diff \
    main.h:type:rtw_5g_ht_ns_pwr_idx_diff \
    main.h:type:rtw_5g_ofdm_ns_pwr_idx_diff \
    main.h:type:rtw_5g_vht_ns_pwr_idx_diff main.h:type:rtw_5g_txpwr_idx \
    main.h:type:rtw_txpwr_idx phy.h:type:rtw_power_params
$x "$work/body.c" \
    phy.c:data:rtw_cck_rates phy.c:data:rtw_ofdm_rates \
    phy.c:data:rtw_ht_1s_rates phy.c:data:rtw_ht_2s_rates \
    phy.c:data:rtw_vht_1s_rates phy.c:data:rtw_vht_2s_rates \
    phy.c:data:rtw_ht_3s_rates phy.c:data:rtw_ht_4s_rates \
    phy.c:data:rtw_vht_3s_rates phy.c:data:rtw_vht_4s_rates \
    phy.c:data:rtw_rate_section phy.c:data:rtw_rate_size \
    phy.c:data:rtw_channel_idx_5g \
    regd.c:type:rtw_regd_alternative_t regd.c:data:rtw_regd_alt \
    regd.c:fn:rtw_regd_has_alt \
    phy.c:fn:tbl_to_dec_pwr_by_rate \
    phy.c:fn:rtw_phy_get_rate_values_of_txpwr_by_rate \
    phy.c:fn:rtw_phy_store_tx_power_by_rate phy.c:fn:rtw_parse_tbl_bb_pg \
    phy.c:fn:rtw_channel_to_idx phy.c:fn:rtw_phy_set_tx_power_limit \
    phy.c:fn:rtw_xref_5g_txpwr_lmt phy.c:fn:rtw_xref_txpwr_lmt_by_rs \
    phy.c:fn:rtw_xref_5g_txpwr_lmt_by_ch phy.c:fn:rtw_xref_txpwr_lmt_by_bw \
    phy.c:fn:rtw_xref_txpwr_lmt phy.c:fn:__cfg_txpwr_lmt_by_alt \
    phy.c:fn:rtw_cfg_txpwr_lmt_by_alt phy.c:fn:rtw_parse_tbl_txpwr_lmt \
    phy.c:fn:rtw_get_channel_group phy.c:fn:rtw_phy_get_dis_dpd_by_rate_diff \
    phy.c:fn:rtw_phy_get_2g_tx_power_index \
    phy.c:fn:rtw_phy_get_5g_tx_power_index \
    phy.c:fn:rtw_phy_rate_to_rate_section phy.c:fn:rtw_phy_get_tx_power_limit \
    phy.c:fn:rtw_phy_get_tx_power_sar phy.c:fn:rtw_get_tx_power_params \
    phy.c:fn:rtw_phy_get_tx_power_index \
    phy.c:fn:rtw_phy_set_tx_power_index_by_rs \
    phy.c:fn:rtw_phy_set_tx_power_level_by_path \
    phy.c:fn:rtw_phy_set_tx_power_level \
    phy.c:fn:rtw_phy_tx_power_by_rate_config_by_path \
    phy.c:fn:rtw_phy_tx_power_by_rate_config \
    phy.c:fn:__rtw_phy_tx_power_limit_config \
    phy.c:fn:rtw_phy_tx_power_limit_config \
    phy.c:fn:rtw_phy_init_tx_power_limit phy.c:fn:rtw_phy_init_tx_power \
    rtw8822c.c:fn:rtw8822c_set_write_tx_power_ref \
    rtw8822c.c:fn:rtw8822c_set_tx_power_diff \
    rtw8822c.c:fn:rtw8822c_set_tx_power_index

${CC:-cc} -std=gnu11 -w -I"$work" -I"$src/sys/dev/ic" -I"$ref" -I"$tools" \
    "$tools/rtw8822c-txpwr-trace-ref.c" "$src/sys/dev/ic/rtw8822c_table.c" \
    -o "$work/gen"
"$work/gen"
