#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: unchanged pinned FreeBSD coex source consumer trace generator.
# Usage: rtw8822c-coex-trace-gen.sh REFDIR > rtw8822c-coex.trace
# Required source revision: a143197801c680b958ab662c88cf086668a1958b.
# This is a Wi-Fi-only slice, not the complete dynamic Bluetooth engine.
set -eu
[ "$#" -eq 1 ] || exit 2
ref=$1
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/rtw8822c-coex-trace.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
check() {
    sum=$(shasum -a 256 "$ref/$1" | cut -d' ' -f1)
    [ "$sum" = "$2" ] || { echo "$1: incorrect SHA256" >&2; exit 1; }
}
check coex.c 3b7def2889fd75382b780437a8d0884f711efbde2961443ecdd6cd91f0353d42
check coex.h 047aa0d252aee946869be49960129a52b5132528563ed52d9355e2b9870b3800
check rtw8822c.c 6986e6107da9261b97277d57fcc11daf5dcb0d71571c55b1080bdea1495ce160
check reg.h c8246f9305539347fdb62b0294fe12ea24769cb3ad56c2b0235c4269a0307324
check util.c f5b17292be0028da0b5296caa4bb062f879a8b92161ccc299e092bb64d39a8a8
check phy.c 669d76c8acfc9f30418dbd119e777eae4404f6e3bb1198292aab536a3aa9e4ec
x="python3 $tools/rtw88-ref-extract.py $ref"
$x "$work/enums.h" coex.h:enums
$x "$work/body.c" \
    util.c:fn:check_hw_ready util.c:fn:ltecoex_read_reg util.c:fn:ltecoex_reg_write \
    coex.c:fn:rtw_coex_read_indirect_reg coex.c:fn:rtw_coex_write_indirect_reg \
    coex.c:fn:rtw_coex_coex_ctrl_owner coex.c:fn:rtw_coex_set_gnt_bt \
    coex.c:fn:rtw_coex_set_gnt_wl coex.c:fn:rtw_coex_set_ant_path \
    coex.c:fn:rtw_coex_set_wl_pri_mask coex.c:fn:rtw_coex_switchband_notify \
    phy.c:fn:rtw_phy_write_rf_reg \
    rtw8822c.c:fn:rtw8822c_coex_cfg_rfe_type \
    rtw8822c.c:fn:rtw8822c_coex_cfg_gnt_debug rtw8822c.c:fn:rtw8822c_coex_cfg_init
${CC:-cc} -std=gnu11 -w ${RTW88_TEST_CFLAGS:-} -I"$ref" -I"$tools" -I"$work" \
    "$tools/rtw8822c-coex-trace-ref.c" -o "$work/gen"
"$work/gen"
