#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD RTL8822CE firmware-bootstrap fake-bus runner, AI-assisted.
set -eu
case ${RTW88_ROUTING_ANALYZE:-0} in 0|1) ;; *) exit 2 ;; esac
# TEST_RUNNER names one executable, for example a Memcheck wrapper.
run_test() {
    if [ -n "${TEST_RUNNER:-}" ]; then
        "$TEST_RUNNER" "$@"
    else
        "$@"
    fi
}
[ "$#" -eq 0 ] || { echo "usage: $0" >&2; exit 2; }
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=${RTW88_ROUTING_SOURCE:-$(CDPATH= cd -- "$tools/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/rtw88-routing-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
mkdir -p "$work/include/sys" "$work/include/dev" "$work/include/net80211"
cp "$src/sys/net80211/_ieee80211.h" "$work/include/net80211/"
python3 "$tools/rtw88-fixed-contract.py" "$src" "$work/include"
python3 "$tools/net80211-wme-contract.py" "$src" "$work/include"
python3 "$tools/rtw88-routing-contract.py" "$src" "$work/include"
${CC:-cc} -std=c11 -DNDEBUG -Wall -Wextra -Werror -Wno-unused-parameter \
    -I"$work/include" "$tools/rtw88-routing-ht20-stub-contract.c" \
    -o "$work/ht20-stub-contract"
run_test "$work/ht20-stub-contract"
ln -s "$src/sys/dev/ic" "$work/include/dev/ic"
for header in sys/param.h sys/systm.h; do
    printf '#include <errno.h>\n#include "rtw88-test-kernel.h"\n' > "$work/include/$header"
done
for altq in 0 1; do
    flags=
    [ "$altq" = 0 ] || flags=-DALTQ
    ${CC:-cc} -std=c11 -Wall -Werror $flags ${RTW88_TEST_CFLAGS:-} \
        -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" \
        "$tools/rtw88-routing-contract.c" \
        "$src/sys/dev/ic/rtw88_net_policy.c" \
        "$src/sys/dev/ic/rtw88.c" \
        "$src/sys/dev/ic/rtw88_hw.c" \
        "$src/sys/dev/ic/rtw88_fw_init.c" \
        "$src/sys/dev/ic/rtw8822c.c" \
        "$src/sys/dev/ic/rtw8822c_boot.c" \
        "$src/sys/dev/ic/rtw8822c_power.c" \
        "$src/sys/dev/ic/rtw8822c_mac.c" \
        "$src/sys/dev/ic/rtw8822c_phy.c" \
        "$src/sys/dev/ic/rtw8822c_dack.c" \
        "$src/sys/dev/ic/rtw8822c_table.c" \
        "$src/sys/dev/ic/rtw8822c_coex.c" \
        "$src/sys/dev/ic/rtw8822c_txpwr.c" \
        -o "$work/test"
    if ! run_test "$work/test" > "$work/output"; then
        cat "$work/output"
        exit 1
    fi
    echo "ALTQ=$altq"
    cat "$work/output"
    if [ "${RTW88_ROUTING_ANALYZE:-0}" = 1 ]; then
        ${CC:-cc} -std=c11 -O0 -g -Wall -Werror -fanalyzer $flags \
            -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" \
            -c "$tools/rtw88-routing-contract.c" -o "$work/analysis.o"
        ${CC:-cc} -std=c11 -dM -E $flags - < /dev/null > "$work/predefines.h"
        cppcheck --inline-suppr --enable=warning,performance,portability --error-exitcode=1 \
            --check-level=exhaustive --include="$work/predefines.h" \
            -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" \
            "$tools/rtw88-routing-contract.c" "$src/sys/dev/ic/rtw88_net_policy.c"
        ${CLANG:-clang} ${RTW88_CLANG_FLAGS:-} --analyze -std=c11 $flags \
            -Wall -Werror -Xanalyzer -analyzer-output=text \
            -Xanalyzer -analyzer-werror \
            -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" \
            "$tools/rtw88-routing-contract.c" -o "$work/routing.plist"
    fi
done
