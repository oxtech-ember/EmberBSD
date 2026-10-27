#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD RTL8822CE firmware-bootstrap fake-bus runner, AI-assisted.
set -eu
case ${RTW88_HWFEATURE_ANALYZE:-0} in 0|1) ;; *) exit 2 ;; esac
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
core_source=${RTW88_CORE_SOURCE:-"$src/sys/dev/ic/rtw88.c"}
work=$(mktemp -d "${TMPDIR:-/tmp}/rtw88-hwfeature-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi

mkdir -p "$work/include/sys" "$work/include/dev"
ln -s "$src/sys/dev/ic" "$work/include/dev/ic"
for header in sys/param.h sys/systm.h; do
    printf '#include <errno.h>\n#include "rtw88-test-kernel.h"\n' > "$work/include/$header"
done
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${RTW88_TEST_CFLAGS:-} \
    -I"$work" -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" \
    "$tools/rtw88-hwfeature-contract.c" \
    "$core_source" \
    "$src/sys/dev/pci/rtw88_pci_hci.c" \
    "$src/sys/dev/pci/rtw88_pci_packet.c" \
    "$src/sys/dev/ic/rtw88_chip.c" \
    "$src/sys/dev/ic/rtw8822c_runtime.c" \
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
if [ "${RTW88_HWFEATURE_ANALYZE:-0}" = 1 ]; then
    printf 'void causal_null(void) { int *p = 0; *p = 1; }\n' > "$work/causal.c"
    if ${CC:-cc} -std=c11 -O0 -Wall -Wextra -Werror -fanalyzer \
        -c "$work/causal.c" -o "$work/causal.o" > "$work/causal.log" 2>&1; then
        echo 'GCC analyzer failed to reject causal NULL' >&2; exit 1
    fi
    grep -F 'analyzer-null-dereference' "$work/causal.log" > /dev/null
    echo 'GCC analyzer causal NULL gate: rejected'
    # Consumer executes actual startup; analyze core and byte decoder too.
    for source in "$tools/rtw88-hwfeature-contract.c" \
        "$src/sys/dev/ic/rtw88.c" "$src/sys/dev/ic/rtw88_chip.c" \
        "$src/sys/dev/ic/rtw8822c_runtime.c" "$src/sys/dev/pci/rtw88_pci_hci.c" \
        "$src/sys/dev/pci/rtw88_pci_packet.c" "$src/sys/dev/ic/rtw88_hw.c" "$src/sys/dev/ic/rtw8822c_hw.c"; do
        ${CC:-cc} -std=c11 -O0 -g -Wall -Wextra -Werror -fanalyzer \
            -I"$work" -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" \
            -c "$source" -o "$work/$(basename "$source").o"
        ${CLANG:-clang} ${RTW88_CLANG_FLAGS:-} --analyze -std=c11 \
            -Wall -Wextra -Werror -Xanalyzer -analyzer-output=text \
            -Xanalyzer -analyzer-werror \
            -I"$work" -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" \
            "$source" -o "$work/$(basename "$source").plist"
    done
    ${CC:-cc} -std=c11 -dM -E - < /dev/null > "$work/predefines.h"
    cppcheck --enable=warning,performance,portability --error-exitcode=1 \
        --check-level=exhaustive --include="$work/predefines.h" \
        -I"$work" -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" \
        "$tools/rtw88-hwfeature-contract.c" "$src/sys/dev/ic/rtw88.c" \
        "$src/sys/dev/ic/rtw88_chip.c" \
        "$src/sys/dev/ic/rtw8822c_runtime.c" \
        "$src/sys/dev/pci/rtw88_pci_hci.c" \
        "$src/sys/dev/pci/rtw88_pci_packet.c" \
        "$src/sys/dev/ic/rtw88_hw.c" "$src/sys/dev/ic/rtw8822c_hw.c"
    # The numeric diagnostic consumer is compiled into the actual PCI fixture.
    # Reuse its explicit kernel-service shim, then analyze that consumer too.
    mkdir -p "$work/include/dev/pci"
    ln -s "$src/sys/dev/pci/pcidevs.h" "$work/include/dev/pci/pcidevs.h"
    for header in sys/device.h sys/kmem.h sys/mutex.h sys/once.h sys/bus.h \
        sys/intr.h sys/xcall.h dev/pci/pcireg.h dev/pci/pcivar.h dev/firmload.h; do
        printf '#include "rtw88-test-kernel.h"\n' > "$work/include/$header"
    done
    ${CC:-cc} -std=c11 -O0 -g -Wall -Wextra -Werror -fanalyzer \
        -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" \
        -c "$tools/rtw88-pci-contract.c" -o "$work/pci-analysis.o"
    cppcheck --enable=warning,performance,portability --error-exitcode=1 \
        --check-level=exhaustive --include="$work/predefines.h" \
        -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" \
        "$tools/rtw88-pci-contract.c"
    ${CLANG:-clang} ${RTW88_CLANG_FLAGS:-} --analyze -std=c11 \
        -Wall -Wextra -Werror -Xanalyzer -analyzer-output=text \
        -Xanalyzer -analyzer-werror \
        -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" \
        "$tools/rtw88-pci-contract.c" -o "$work/pci.plist"

fi
