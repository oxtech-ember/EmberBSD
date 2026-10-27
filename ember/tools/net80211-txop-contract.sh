#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
: "${CC:?select CC using the SDK run-native-check command}"
case ${TXOP_ANALYZE:-0} in 0|1) ;; *) exit 2 ;; esac
[ "$#" -eq 0 ] || exit 2
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/net80211-txop.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
python3 "$tools/net80211-txop-contract.py" "$src" "$work"
"$CC" -std=c11 -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE \
    -Wall -Werror ${TXOP_TEST_CFLAGS:-} -I"$work" \
    "$tools/net80211-txop-contract.c" -o "$work/test"
if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/test"; else "$work/test"; fi
"$CC" -std=c11 -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE \
    -Wall -Werror ${TXOP_TEST_CFLAGS:-} -I"$work" \
    "$tools/net80211-txop-drivers-contract.c" -o "$work/drivers"
if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/drivers"; else "$work/drivers"; fi
"$CC" -std=c11 -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE \
    -Wall -Werror ${TXOP_TEST_CFLAGS:-} -I"$work" \
    "$tools/net80211-txop-intel-contract.c" -o "$work/intel"
if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/intel"; else "$work/intel"; fi
if [ "${TXOP_ANALYZE:-0}" = 1 ]; then
    "$CC" -std=c11 -dM -E - < /dev/null > "$work/predefines.h"
    for unit in net80211-txop-contract net80211-txop-drivers-contract net80211-txop-intel-contract; do
        cppcheck --enable=warning,performance,portability --error-exitcode=1 \
            --check-level=exhaustive --include="$work/predefines.h" \
            -I"$work" "$tools/$unit.c"
        "${CSA:?select clang using SDK --required-role clang}" ${TXOP_CLANG_FLAGS:-} --analyze -std=c11 \
            -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -Wall -Werror \
            -Xanalyzer -analyzer-output=text -Xanalyzer -analyzer-werror \
            -I"$work" "$tools/$unit.c" -o "$work/$unit.plist"
    done
fi
