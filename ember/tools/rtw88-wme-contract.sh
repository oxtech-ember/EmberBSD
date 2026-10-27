#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
: "${CC:?select CC using the SDK run-native-check command}"
case ${RTW88_WME_ANALYZE:-0} in 0|1) ;; *) exit 2 ;; esac
[ "$#" -eq 0 ] || exit 2
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/rtw88-wme.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
python3 "$tools/rtw88-wme-contract.py" "$src" "$work"
mkdir -p "$work/dev"
ln -s "$src/sys/dev/ic" "$work/dev/ic"
"$CC" -std=c11 -Wall -Wextra -Werror ${RTW88_TEST_CFLAGS:-} \
    -I"$work" -I"$tools" "$tools/rtw88-wme-contract.c" \
    "$src/sys/dev/ic/rtw88_hw.c" -o "$work/test"
if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/test"; else "$work/test"; fi
if [ "${RTW88_WME_ANALYZE:-0}" = 1 ]; then
    "$CC" -std=c11 -dM -E - < /dev/null > "$work/predefines.h"
    cppcheck --enable=warning,performance,portability --error-exitcode=1 \
        --check-level=exhaustive --include="$work/predefines.h" \
        -I"$work" -I"$tools" "$tools/rtw88-wme-contract.c" \
        "$src/sys/dev/ic/rtw88_hw.c"
    "${CSA:-clang}" ${RTW88_CLANG_FLAGS:-} --analyze -std=c11 -Wall -Wextra -Werror \
        -Xanalyzer -analyzer-output=text -Xanalyzer -analyzer-werror \
        -I"$work" -I"$tools" "$tools/rtw88-wme-contract.c" \
        -o "$work/core.plist"
    "${CSA:-clang}" ${RTW88_CLANG_FLAGS:-} --analyze -std=c11 -Wall -Wextra -Werror \
        -Xanalyzer -analyzer-output=text -Xanalyzer -analyzer-werror \
        "$src/sys/dev/ic/rtw88_hw.c" -o "$work/hw.plist"
fi
