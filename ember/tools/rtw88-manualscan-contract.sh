#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD RTL8822CE firmware-bootstrap fake-bus runner, AI-assisted.
set -eu
case ${RTW88_START_ANALYZE:-0} in 0|1) ;; *) exit 2 ;; esac
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
work=$(mktemp -d "${TMPDIR:-/tmp}/rtw88-manualscan-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
python3 "$tools/rtw88-state-contract.py" "$src" "$work"
python3 "$tools/rtw88-manualscan-contract.py" "$src" "$work"
mkdir -p "$work/include/sys" "$work/include/dev"
ln -s "$src/sys/dev/ic" "$work/include/dev/ic"
for header in sys/param.h sys/systm.h; do
    printf '#include <errno.h>\n#include "rtw88-test-kernel.h"\n' > "$work/include/$header"
done
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${RTW88_TEST_CFLAGS:-} \
    -I"$work" -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" \
    "$tools/rtw88-manualscan-contract.c" \
    "$src/sys/dev/ic/rtw88.c" \
    "$src/sys/dev/ic/rtw88_chip.c" \
    "$src/sys/dev/ic/rtw8822c_runtime.c" \
    "$src/sys/dev/pci/rtw88_pci_hci.c" \
    "$src/sys/dev/pci/rtw88_pci_packet.c" \
    "$src/sys/dev/ic/rtw88_net_policy.c" \
    "$src/sys/dev/ic/rtw88_hw.c" "$src/sys/dev/ic/rtw8822c_hw.c" \
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
cat "$work/output"

if [ "${RTW88_START_ANALYZE:-0}" = 1 ]; then
    printf 'void causal_null(void) { int *p = 0; *p = 1; }\n' > "$work/causal.c"
    if ${CC:-cc} -std=c11 -O0 -Wall -Wextra -Werror -fanalyzer \
        -c "$work/causal.c" -o "$work/causal.o" > "$work/causal.log" 2>&1; then
        echo 'GCC analyzer failed to reject causal NULL' >&2; exit 1
    fi
    grep -F 'analyzer-null-dereference' "$work/causal.log" > /dev/null
    echo 'GCC analyzer causal NULL gate: rejected'
    # Analyze the complete extracted worker in its executable source consumer.
    # O0 avoids compiler-optimization artifacts in this host kernel model.
    ${CC:-cc} -std=c11 -O0 -g -Wall -Wextra -Werror -fanalyzer \
        -I"$work" -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" \
        -c "$tools/rtw88-manualscan-contract.c" -o "$work/analysis.o"
    ${CC:-cc} -std=c11 -O0 -g -Wall -Wextra -Werror -fanalyzer \
        -I"$work" -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" \
        -c "$src/sys/dev/ic/rtw88.c" -o "$work/core-analysis.o"
    ${CC:-cc} -std=c11 -O0 -g -Wall -Wextra -Werror -fanalyzer \
        -I"$work" -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" \
        -c "$src/sys/dev/pci/rtw88_pci_hci.c" -o "$work/pci-hci-analysis.o"
    ${CC:-cc} -std=c11 -O0 -g -Wall -Wextra -Werror -fanalyzer \
        -I"$work" -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" \
        -c "$src/sys/dev/pci/rtw88_pci_packet.c" -o "$work/pci-packet-analysis.o"
    ${CC:-cc} -std=c11 -dM -E - < /dev/null > "$work/predefines.h"
    cppcheck --enable=warning,performance,portability --error-exitcode=1 \
        --check-level=exhaustive --include="$work/predefines.h" \
        -I"$work" -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" \
        "$tools/rtw88-manualscan-contract.c" "$src/sys/dev/ic/rtw88.c" \
        "$src/sys/dev/ic/rtw88_chip.c" \
        "$src/sys/dev/ic/rtw8822c_runtime.c" \
        "$src/sys/dev/pci/rtw88_pci_hci.c" \
        "$src/sys/dev/pci/rtw88_pci_packet.c" \
        "$src/sys/dev/ic/rtw88_net_policy.c"
    ${CLANG:-clang} ${RTW88_CLANG_FLAGS:-} --analyze -std=c11 \
        -Wall -Wextra -Werror -Xanalyzer -analyzer-output=text \
        -Xanalyzer -analyzer-werror \
        -I"$work" -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" \
        "$tools/rtw88-manualscan-contract.c" -o "$work/state.plist"
    ${CLANG:-clang} ${RTW88_CLANG_FLAGS:-} --analyze -std=c11 \
        -Wall -Wextra -Werror -Xanalyzer -analyzer-output=text \
        -Xanalyzer -analyzer-werror \
        -I"$work" -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" \
        "$src/sys/dev/ic/rtw88.c" -o "$work/core.plist"
    ${CLANG:-clang} ${RTW88_CLANG_FLAGS:-} --analyze -std=c11 \
        -Wall -Wextra -Werror -Xanalyzer -analyzer-output=text \
        -Xanalyzer -analyzer-werror \
        -I"$work" -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" \
        "$src/sys/dev/pci/rtw88_pci_hci.c" -o "$work/pci-hci.plist"
    ${CLANG:-clang} ${RTW88_CLANG_FLAGS:-} --analyze -std=c11 \
        -Wall -Wextra -Werror -Xanalyzer -analyzer-output=text \
        -Xanalyzer -analyzer-werror \
        -I"$work" -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" \
        "$src/sys/dev/pci/rtw88_pci_packet.c" -o "$work/pci-packet.plist"
fi
