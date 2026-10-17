#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: unchanged pinned normal PHY table dispatch producer, AI-assisted.
# Requires FreeBSD a143197801c680b958ab662c88cf086668a1958b rtw88 sources.
set -eu
[ "$#" -eq 1 ] || exit 2
ref=$1
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/rtw8822c-init-trace.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
[ "$(shasum -a 256 "$ref/phy.c" | cut -d' ' -f1)" = \
    669d76c8acfc9f30418dbd119e777eae4404f6e3bb1198292aab536a3aa9e4ec ]
[ "$(shasum -a 256 "$ref/rtw8822c.c" | cut -d' ' -f1)" = \
    6986e6107da9261b97277d57fcc11daf5dcb0d71571c55b1080bdea1495ce160 ]
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
# Reuse the existing upstream-source extraction dependency.
python3 "$tools/rtw88-ref-extract.py" "$ref" "$work/body.c" \
    phy.c:fn:rtw_load_rfk_table phy.c:fn:rtw_phy_load_tables
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${RTW88_TEST_CFLAGS:-} \
    -I"$work" "$tools/rtw8822c-init-trace-ref.c" -o "$work/gen"
"$work/gen"
