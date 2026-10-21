#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
[ "$#" -eq 0 ] || exit 2
case ${RTW88_RECONNECT_ANALYZE:-0} in 0|1) ;; *) exit 2 ;; esac
case ${RTW88_APLOSS_MUTANT:-none} in none|supervision|withdrawal|ra|notification) ;; *) exit 2 ;; esac
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/rtw88-aploss-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
python3 "$tools/rtw88-aploss-contract.py" "$src" "$work" "${RTW88_APLOSS_MUTANT:-none}"
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${RTW88_TEST_CFLAGS:-} \
    -I"$work" -I"$src/sys/dev/ic" "$tools/rtw88-aploss-contract.c" \
    "$src/sys/dev/ic/rtw88_net_policy.c" -o "$work/test"
if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/test"; else "$work/test"; fi
if [ "${RTW88_APLOSS_MUTANT:-none}" = none ]; then
    for mutant in supervision withdrawal ra notification; do
        mkdir "$work/$mutant"
        python3 "$tools/rtw88-aploss-contract.py" "$src" "$work/$mutant" "$mutant"
        ${CC:-cc} -std=c11 -Wall -Wextra -Werror ${RTW88_TEST_CFLAGS:-} \
            -I"$work/$mutant" -I"$src/sys/dev/ic" "$tools/rtw88-aploss-contract.c" \
            "$src/sys/dev/ic/rtw88_net_policy.c" -o "$work/$mutant/test"
        status=0
        if [ -n "${TEST_RUNNER:-}" ]; then
            "$TEST_RUNNER" "$work/$mutant/test" > "$work/$mutant/log" 2>&1 || status=$?
        else
            "$work/$mutant/test" > "$work/$mutant/log" 2>&1 || status=$?
        fi
        if [ "$status" != 1 ] || ! grep -E '^rtw88 AP-loss chain: [0-9]+ checks, [1-9][0-9]* failures$' "$work/$mutant/log"; then
            cat "$work/$mutant/log"; echo "Causal mutant $mutant did not fail functionally" >&2; exit 1
        fi
        echo "Causal mutant $mutant: rejected"
    done
fi
if [ "${RTW88_RECONNECT_ANALYZE:-0}" = 1 ]; then
    printf 'void causal_null(void) { int *p = 0; *p = 1; }\n' > "$work/causal.c"
    if ${CC:-cc} -std=c11 -O0 -Wall -Wextra -Werror -fanalyzer \
        -c "$work/causal.c" -o "$work/causal.o" > "$work/causal.log" 2>&1; then
        echo 'GCC analyzer failed to reject causal NULL' >&2; exit 1
    fi
    grep -F 'analyzer-null-dereference' "$work/causal.log" > /dev/null
    echo 'GCC analyzer causal NULL: rejected'
    ${CC:-cc} -std=c11 -O0 -g -Wall -Wextra -Werror -fanalyzer \
        -I"$work" -I"$src/sys/dev/ic" -c "$tools/rtw88-aploss-contract.c" -o "$work/analysis.o"
    ${CC:-cc} -std=c11 -dM -E - < /dev/null > "$work/predefines.h"
    cppcheck --enable=warning,performance,portability --error-exitcode=1 \
        --check-level=exhaustive --include="$work/predefines.h" -I"$work" \
        -I"$src/sys/dev/ic" "$tools/rtw88-aploss-contract.c" "$src/sys/dev/ic/rtw88_net_policy.c"
    ${CLANG:-clang} ${RTW88_CLANG_FLAGS:-} --analyze -std=c11 \
        -Wall -Wextra -Werror -Xanalyzer -analyzer-output=text \
        -Xanalyzer -analyzer-werror -I"$work" -I"$src/sys/dev/ic" \
        "$tools/rtw88-aploss-contract.c" -o "$work/aploss.plist"
fi
