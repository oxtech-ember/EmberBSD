#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Actual CCMP, ioctl, replay event and AES-CCM source; modeled kernel services.
set -eu
[ "$#" -eq 0 ] || exit 2
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/ember-ccmp.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
case "${CCMP_ANALYZE:-0}" in 0|1) ;; *) exit 2 ;; esac
python3 "$tools/ccmp-contract-extract.py" "$src" "$work"
${CC:-gcc-16} -std=c11 -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -Wall -Werror \
    ${CCMP_TEST_CFLAGS:-} -I"$work" -I"$tools" \
    "$tools/ccmp-contract.c" -o "$work/test"
if [ -n "${TEST_RUNNER:-}" ]; then
    "$TEST_RUNNER" "$work/test"
else
    "$work/test"
fi
if [ "${CCMP_ANALYZE:-0}" = 1 ]; then
    ${CC:-gcc-16} -std=c11 -dM -E - < /dev/null > "$work/predefines.h"
    cppcheck --enable=warning,performance,portability --error-exitcode=1 \
        --check-level=exhaustive --include="$work/predefines.h" \
        -I"$work" -I"$tools" "$tools/ccmp-contract.c"
    ${CLANG:-clang} --analyze -std=c11 -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -Wall -Werror -Xanalyzer -analyzer-output=text \
        -Xanalyzer -analyzer-werror -I"$work" -I"$tools" \
        "$tools/ccmp-contract.c" -o "$work/analysis.plist"
fi
