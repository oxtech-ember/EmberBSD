#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
HT20_TEST_CFLAGS=${HT20_TEST_CFLAGS:-${RTW88_TEST_CFLAGS:-}}
case ${HT20_ANALYZE:-0} in
0|1) ;;
*) exit 2 ;;
esac
[ "$#" -eq 0 ] || exit 2
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/net80211-wme.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path)
    export SDKROOT
fi
python3 "$tools/net80211-ht20-contract.py" "$src" "$work"
${CC:-gcc-16} -std=c11 -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -Wall -Werror ${HT20_TEST_CFLAGS:-} \
    -I"$work" "$tools/net80211-ht20-contract.c" -o "$work/test"
if [ -n "${TEST_RUNNER:-}" ]; then
    "$TEST_RUNNER" "$work/test"
else
    "$work/test"
fi
if [ "${HT20_ANALYZE:-0}" = 1 ]; then
    printf 'void causal_null(void) { int *p=0; *p=1; }\n' > "$work/causal.c"
    if ${CC:-gcc-16} -O0 -Wall -Werror -fanalyzer -c "$work/causal.c" \
        -o "$work/causal.o" > "$work/causal.log" 2>&1; then
        echo 'GCC analyzer failed to reject causal NULL' >&2; exit 1
    fi
    grep -F 'analyzer-null-dereference' "$work/causal.log" > /dev/null
    echo 'GCC analyzer causal NULL gate: rejected'
    ${CC:-gcc-16} -std=c11 -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE \
        -O0 -g -Wall -Werror -fanalyzer -I"$work" \
        -c "$tools/net80211-ht20-contract.c" -o "$work/analysis.o"

    ${CC:-gcc-16} -std=c11 -dM -E - < /dev/null > "$work/predefines.h"
    cppcheck --enable=warning,performance,portability --error-exitcode=1 \
        --check-level=exhaustive --include="$work/predefines.h" \
        -I"$work" "$tools/net80211-ht20-contract.c"
    ${CLANG:-clang} ${HT20_CLANG_FLAGS:-} --analyze -std=c11 \
        -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -Wall -Werror \
        -Xanalyzer -analyzer-output=text -Xanalyzer -analyzer-werror \
        -I"$work" "$tools/net80211-ht20-contract.c" -o "$work/wme.plist"
fi
