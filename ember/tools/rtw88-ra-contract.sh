#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD RTL8822CE firmware-bootstrap fake-bus runner, AI-assisted.
set -eu
case ${RTW88_RA_ANALYZE:-0} in 0|1) ;; *) exit 2 ;; esac
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
src=$(CDPATH= cd -- "$tools/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/rtw88-ra-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
python3 "$tools/rtw88-state-contract.py" "$src" "$work"
mkdir -p "$work/include/sys" "$work/include/dev"
ln -s "$src/sys/dev/ic" "$work/include/dev/ic"
for header in sys/param.h sys/systm.h; do
    printf '#include <errno.h>\n#include "rtw88-test-kernel.h"\n' > "$work/include/$header"
done
# The default build has no VHT entry; the experiment build owns VHT80.
for vht in 0 1; do
    vflag=-URTW88_VHT_EXPERIMENT
    [ "$vht" = 0 ] || vflag=-DRTW88_VHT_EXPERIMENT
    ${CC:-cc} -std=c11 -Wall -Wextra -Werror $vflag ${RTW88_TEST_CFLAGS:-} \
        -I"$work" -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" \
        "$tools/rtw88-ra-contract.c" \
        "$src/sys/dev/ic/rtw88.c" \
        "$src/sys/dev/ic/rtw88_net_policy.c" \
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
    echo "VHT=$vht"
    cat "$work/output"
    want=disabled
    [ "$vht" = 0 ] || want=enabled
    grep -F "rtw88 VHT80 RA and TX formats: $want" "$work/output" > /dev/null
    # Both real calibration quality scenarios must reach the startup warning.
    grep -F 'WARNING: cold calibration degraded: ADC nonconverged 0x1 DAC nonconverged 0; RF quality is unqualified' "$work/output" > /dev/null
    grep -F 'WARNING: cold calibration degraded: ADC nonconverged 0 DAC nonconverged 0x1; RF quality is unqualified' "$work/output" > /dev/null
done
# VHT80 exists only for the explicit experiment outside the RFK envelope.
printf '#include <dev/ic/rtw88var.h>\n#if defined(RTW88_VHT80) != EXPECT\n#error RTW88_VHT80 gate\n#endif\n' > "$work/vht-gate.c"
for gate in '1 -DRTW88_VHT_EXPERIMENT' '0 -DRTW88_VHT_EXPERIMENT -DRTW88_RFK_EXPERIMENT' \
    '0 -DRTW88_RFK_EXPERIMENT' '0'; do
    set -- $gate
    expect=$1; shift
    ${CC:-cc} -std=c11 -E -DEXPECT="$expect" "$@" \
        -I"$work" -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" \
        "$work/vht-gate.c" -o /dev/null
done
echo 'VHT80 gate: experiment only, never with RFK'

if [ "${RTW88_RA_ANALYZE:-0}" = 1 ]; then
    printf 'void causal_null(void) { int *p = 0; *p = 1; }\n' > "$work/causal.c"
    if ${CC:-cc} -std=c11 -O0 -Wall -Wextra -Werror -fanalyzer \
        -c "$work/causal.c" -o "$work/causal.o" > "$work/causal.log" 2>&1; then
        echo 'GCC analyzer failed to reject causal NULL' >&2; exit 1
    fi
    grep -F 'analyzer-null-dereference' "$work/causal.log" > /dev/null
    echo 'GCC analyzer causal NULL gate: rejected'
    # Analyze the complete extracted worker in its executable source consumer.
    # O0 avoids compiler-optimization artifacts in this host kernel model.
    for vflag in -URTW88_VHT_EXPERIMENT -DRTW88_VHT_EXPERIMENT; do
        ${CC:-cc} -std=c11 -O0 -g -Wall -Wextra -Werror -fanalyzer $vflag \
            -I"$work" -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" \
            -c "$tools/rtw88-ra-contract.c" -o "$work/analysis.o"
        ${CC:-cc} -std=c11 -O0 -g -Wall -Wextra -Werror -fanalyzer $vflag \
            -I"$work" -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" \
            -c "$src/sys/dev/ic/rtw88.c" -o "$work/core-analysis.o"
    done
    ${CC:-cc} -std=c11 -dM -E - < /dev/null > "$work/predefines.h"
    for vflag in -URTW88_VHT_EXPERIMENT -DRTW88_VHT_EXPERIMENT; do
        cppcheck --enable=warning,performance,portability --error-exitcode=1 \
            --check-level=exhaustive --include="$work/predefines.h" $vflag \
            -I"$work" -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" \
            "$tools/rtw88-ra-contract.c" "$src/sys/dev/ic/rtw88.c" \
            "$src/sys/dev/ic/rtw88_net_policy.c"
        ${CLANG:-clang} ${RTW88_CLANG_FLAGS:-} --analyze -std=c11 $vflag \
            -Wall -Wextra -Werror -Xanalyzer -analyzer-output=text \
            -Xanalyzer -analyzer-werror \
            -I"$work" -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" \
            "$tools/rtw88-ra-contract.c" -o "$work/state.plist"
        ${CLANG:-clang} ${RTW88_CLANG_FLAGS:-} --analyze -std=c11 $vflag \
            -Wall -Wextra -Werror -Xanalyzer -analyzer-output=text \
            -Xanalyzer -analyzer-werror \
            -I"$work" -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" \
            "$src/sys/dev/ic/rtw88.c" -o "$work/core.plist"
    done
fi
