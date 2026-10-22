#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
[ "$#" -eq 0 ] || exit 2
case ${RXBA_ANALYZE:-0} in 0|1) ;; *) exit 2 ;; esac
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/net80211-rxba.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
python3 "$tools/net80211-rxba-contract.py" "$src" "$work"
${CC:-gcc-16} -std=c11 -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -Wall -Werror ${RXBA_TEST_CFLAGS:-${RTW88_TEST_CFLAGS:-${CCMP_TEST_CFLAGS:-}}} \
    -I"$work" -I"$tools" "$tools/net80211-rxba-contract.c" -o "$work/test"
if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/test"; else "$work/test"; fi

${CC:-gcc-16} -std=c11 -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -Wall -Werror ${RXBA_TEST_CFLAGS:-${RTW88_TEST_CFLAGS:-${CCMP_TEST_CFLAGS:-}}} \
    -I"$work" -I"$tools" -I"$src/sys/dev/ic" "$tools/net80211-rxba-native-contract.c" "$src/sys/dev/ic/rtw88_net_policy.c" -o "$work/native-test"
if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/native-test"; else "$work/native-test"; fi

if [ "${RXBA_ANALYZE:-0}" = 1 ]; then
    printf 'void causal_null(void) { int *p=0; *p=1; }\n' > "$work/causal.c"
    if ${CC:-gcc-16} -O0 -Wall -Werror -fanalyzer -c "$work/causal.c" \
        -o "$work/causal.o" > "$work/causal.log" 2>&1; then
        echo 'GCC analyzer failed to reject causal NULL' >&2; exit 1
    fi
    grep -F 'analyzer-null-dereference' "$work/causal.log" > /dev/null
    echo 'GCC analyzer causal NULL gate: rejected'
    for consumer in net80211-rxba-contract.c net80211-rxba-native-contract.c; do
        ${CC:-gcc-16} -std=c11 -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -O0 -g -Wall -Werror -fanalyzer \
            -I"$work" -I"$tools" -I"$src/sys/dev/ic" -c "$tools/$consumer" -o "$work/analysis.o"
        ${CC:-gcc-16} -std=c11 -dM -E - < /dev/null > "$work/predefines.h"
        cppcheck --enable=warning,performance,portability --error-exitcode=1 \
            --check-level=exhaustive --include="$work/predefines.h" \
            -I"$work" -I"$tools" -I"$src/sys/dev/ic" "$tools/$consumer"
        ${CLANG:-clang} ${RTW88_CLANG_FLAGS:-} --analyze -std=c11 -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE \
            -Wall -Werror -Xanalyzer -analyzer-output=text -Xanalyzer -analyzer-werror \
            -I"$work" -I"$tools" -I"$src/sys/dev/ic" "$tools/$consumer" -o "$work/analysis.plist"
    done
fi
