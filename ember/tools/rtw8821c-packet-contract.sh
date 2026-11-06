#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
: "${CC:?select CC using SDK run-native-check}"
case ${RTW8821C_PACKET_ANALYZE:-0} in 0|1) ;; *) exit 2 ;; esac
[ "$#" -eq 0 ] || exit 2
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/rtw8821c-packet.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
"$CC" -std=c11 -Wall -Wextra -Werror ${RTW88_TEST_CFLAGS:-} \
    -I"$src/sys/dev/ic" "$tools/rtw8821c-packet-contract.c" \
    "$src/sys/dev/ic/rtw88_hw.c" "$src/sys/dev/ic/rtw8821c_hw.c" \
    "$src/sys/dev/ic/rtw8822c_hw.c" \
    -o "$work/test"
if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/test"; else "$work/test"; fi
if [ "${RTW8821C_PACKET_ANALYZE:-0}" = 1 ]; then
    "$CC" -std=c11 -dM -E - < /dev/null > "$work/predefines.h"
    cppcheck --enable=warning,performance,portability --error-exitcode=1 \
        --check-level=exhaustive --include="$work/predefines.h" \
        -I"$src/sys/dev/ic" "$tools/rtw8821c-packet-contract.c" \
        "$src/sys/dev/ic/rtw88_hw.c" "$src/sys/dev/ic/rtw8821c_hw.c" \
        "$src/sys/dev/ic/rtw8822c_hw.c"
    for unit in "$tools/rtw8821c-packet-contract.c" \
        "$src/sys/dev/ic/rtw88_hw.c" "$src/sys/dev/ic/rtw8821c_hw.c" \
        "$src/sys/dev/ic/rtw8822c_hw.c"; do
        name=${unit##*/}
        "${CSA:-clang}" ${RTW88_CLANG_FLAGS:-} --analyze -std=c11 -Wall -Wextra -Werror \
            -Xanalyzer -analyzer-output=text -Xanalyzer -analyzer-werror \
            -I"$src/sys/dev/ic" "$unit" -o "$work/$name.plist"
    done
fi
