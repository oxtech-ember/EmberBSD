#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: unchanged pinned FreeBSD coex source consumer trace generator.
# Usage: rtw8821c-coex-trace-gen.sh REFDIR > rtw8821c-coex.trace
# Required source revision: a143197801c680b958ab662c88cf086668a1958b.
# This is a Wi-Fi-only slice, not the complete dynamic Bluetooth engine.
set -eu
[ "$#" -eq 1 ] || exit 2
ref=$1
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/rtw8821c-coex-trace.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
check() {
    sum=$(shasum -a 256 "$ref/$1" | cut -d' ' -f1)
    [ "$sum" = "$2" ] || { echo "$1: incorrect SHA256" >&2; exit 1; }
}
check coex.c 3b7def2889fd75382b780437a8d0884f711efbde2961443ecdd6cd91f0353d42
check coex.h 047aa0d252aee946869be49960129a52b5132528563ed52d9355e2b9870b3800
check rtw8821c.c a51946d4496d8856bbf01010f1786230478b951c4e034b67457a47d4acda414b
check rtw8821c.h 10a311ec1135e887d41bb0977c57375fd9b9a0707e3cda1f5eaa53d20eef97c1
check reg.h c8246f9305539347fdb62b0294fe12ea24769cb3ad56c2b0235c4269a0307324
check util.c f5b17292be0028da0b5296caa4bb062f879a8b92161ccc299e092bb64d39a8a8
check phy.c 669d76c8acfc9f30418dbd119e777eae4404f6e3bb1198292aab536a3aa9e4ec
x="${PYTHON:?select SDK Python} $tools/rtw88-ref-extract.py $ref"
$x "$work/enums.h" coex.h:enums
sed -n '/^#define BIT_MASK_R_RFE_SEL_15/,/^#define CTRL_TYPE_MASK/p' "$ref/rtw8821c.h" >> "$work/enums.h"
$x "$work/body.c" \
    util.c:fn:check_hw_ready util.c:fn:ltecoex_read_reg util.c:fn:ltecoex_reg_write \
    coex.c:fn:rtw_coex_read_indirect_reg coex.c:fn:rtw_coex_write_indirect_reg \
    rtw8821c.c:fn:rtw8821c_coex_cfg_ant_switch \
    coex.c:fn:rtw_coex_coex_ctrl_owner coex.c:fn:rtw_coex_set_gnt_bt \
    coex.c:fn:rtw_coex_set_gnt_wl coex.c:fn:rtw_coex_set_ant_path \
    coex.c:fn:rtw_coex_set_wl_pri_mask coex.c:fn:rtw_coex_switchband_notify \
    phy.c:fn:rtw_phy_write_rf_reg \
    rtw8821c.c:fn:rtw8821c_coex_cfg_rfe_type \
    rtw8821c.c:fn:rtw8821c_coex_cfg_gnt_debug rtw8821c.c:fn:rtw8821c_coex_cfg_init
"${CC:?select CC using SDK run-native-check}" -std=gnu11 -w ${RTW88_TEST_CFLAGS:-} -I"$ref" -I"$tools" -I"$work" \
    "$tools/rtw8821c-coex-trace-ref.c" -o "$work/gen"
"$work/gen"
