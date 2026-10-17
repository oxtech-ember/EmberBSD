#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
[ "$#" -eq 0 ] || exit 2
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/rtw88-net-stop.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
case "${RTW88_STOP_ANALYZE:-0}" in 0|1) ;; *) exit 2 ;; esac
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
# Preserve notices and the complete consumer body. Kernel service/layout
# models are explicit in the harness; no target headers shadow host libc.
awk 'BEGIN { copying=1 } copying { print; if ($0 == " */") copying=0 }' \
    "$src/sys/dev/ic/rtw88_net.c" > "$work/stop-native.inc"
awk '/^rtw88_net_ioctl\(/ { print "static int"; copying=1 }
    copying { print } copying && /^}/ { exit }' \
    "$src/sys/dev/ic/rtw88_net.c" >> "$work/stop-native.inc"
awk '/^#define[[:space:]]+IFF_(UP|RUNNING)[[:space:]]/ { print }' \
    "$src/sys/net/if.h" > "$work/stop-constants.inc"
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${RTW88_TEST_CFLAGS:-} \
    -I"$work" "$tools/rtw88-net-stop-contract.c" -o "$work/test"
if [ -n "${TEST_RUNNER:-}" ]; then
    "$TEST_RUNNER" "$work/test"
else
    "$work/test"
fi
if [ "${RTW88_STOP_ANALYZE:-0}" = 1 ]; then
    ${CC:-cc} -std=c11 -dM -E - < /dev/null > "$work/predefines.h"
    cppcheck --enable=warning,performance,portability --error-exitcode=1 \
        --check-level=exhaustive --include="$work/predefines.h" \
        -I"$work" "$tools/rtw88-net-stop-contract.c"
    ${CLANG:-clang} --analyze -std=c11 -Wall -Wextra -Werror \
        -Xanalyzer -analyzer-output=text -Xanalyzer -analyzer-werror \
        -I"$work" "$tools/rtw88-net-stop-contract.c" -o "$work/analysis.plist"
fi
CIX_STOP_PROBE="$work/test" "${RUBY:-ruby}" "$src/ember/image/test-cix-p1-startup.rb"
