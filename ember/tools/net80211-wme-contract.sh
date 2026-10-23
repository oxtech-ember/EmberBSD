#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
case ${WME_ANALYZE:-0} in
0|1) ;;
*) exit 2 ;;
esac
[ "$#" -eq 0 ] || exit 2
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
work=$(mktemp -d "${TEST_OBJDIR:-${TMPDIR:-/tmp}}/net80211-wme.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path)
    export SDKROOT
fi
python3 "$tools/net80211-wme-contract.py" "$src" "$work"
${CC:-gcc-16} -std=c11 -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -Wall -Werror ${WME_TEST_CFLAGS:-} \
    -I"$work" "$tools/net80211-wme-contract.c" -o "$work/test"
if [ -n "${TEST_RUNNER:-}" ]; then
    "$TEST_RUNNER" "$work/test"
else
    "$work/test"
fi
if [ "${WME_ANALYZE:-0}" = 1 ]; then
    ${CC:-gcc-16} -std=c11 -dM -E - < /dev/null > "$work/predefines.h"
    cppcheck --enable=warning,performance,portability --error-exitcode=1 \
        --check-level=exhaustive --include="$work/predefines.h" \
        -I"$work" "$tools/net80211-wme-contract.c"
    ${CLANG:-clang} ${WME_CLANG_FLAGS:-} --analyze -std=c11 \
        -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -Wall -Werror \
        -Xanalyzer -analyzer-output=text -Xanalyzer -analyzer-werror \
        -I"$work" "$tools/net80211-wme-contract.c" -o "$work/wme.plist"
fi
