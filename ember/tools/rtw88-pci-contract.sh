#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD RTL8822CE actual-source fake-bus runner, AI-assisted.
set -eu
case ${RTW88_PCI_ANALYZE:-0} in 0|1) ;; *) exit 2 ;; esac
case ${RTW88_PCI_RFK:-0} in 0) rfk_flags= ;; 1) rfk_flags=-DRTW88_RFK_EXPERIMENT ;; *) exit 2 ;; esac
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
work=$(mktemp -d "${TMPDIR:-/tmp}/rtw88-pci-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
mkdir -p "$work/include/sys" "$work/include/dev/pci"
printf '#include "rtw88-rfk-atomic-model.h"\n' > "$work/include/sys/atomic.h"
ln -s "$src/sys/dev/ic" "$work/include/dev/ic"
ln -s "$src/sys/dev/pci/pcidevs.h" "$work/include/dev/pci/pcidevs.h"
mkdir -p "$work/include/dev"
for header in sys/param.h sys/systm.h sys/device.h sys/kmem.h \
    sys/mutex.h sys/once.h sys/bus.h sys/intr.h sys/xcall.h dev/pci/pcireg.h \
    dev/pci/pcivar.h dev/firmload.h; do
    printf '#include "rtw88-test-kernel.h"\n' > "$work/include/$header"
done
# RFK's heap allocator is a PCI-test service; do not redefine KM_SLEEP in
# the shared kernel layout used by the independent state-model consumers.
printf '#include "rtw88-test-kernel.h"\n#define KM_SLEEP 0\n' > "$work/include/sys/kmem.h"
${CC:-cc} -std=c11 $rfk_flags -Wall -Wextra -Werror ${RTW88_TEST_CFLAGS:-} \
    -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" \
    "$tools/rtw88-pci-contract.c" "$src/sys/dev/ic/rtw88_hw.c" \
    "$src/sys/dev/ic/rtw88_fw_init.c" \
    "$src/sys/dev/ic/rtw8822c_rfk.c" \
    "$src/sys/dev/ic/rtw8822c.c" "$src/sys/dev/ic/rtw8822c_boot.c" \
    "$src/sys/dev/ic/rtw8822c_power.c" \
    "$src/sys/dev/ic/rtw8822c_mac.c" \
    "$src/sys/dev/ic/rtw8822c_phy.c" \
    "$src/sys/dev/ic/rtw8822c_dack.c" \
    "$src/sys/dev/ic/rtw8822c_table.c" \
    "$src/sys/dev/ic/rtw8822c_coex.c" \
    "$src/sys/dev/ic/rtw8822c_txpwr.c" \
    -o "$work/test"
run_test "$work/test"

if [ "${RTW88_PCI_ANALYZE:-0}" = 1 ]; then
    # This consumer includes the actual core and complete native PCI source.
    ${CC:-cc} -std=c11 $rfk_flags -O0 -g -Wall -Wextra -Werror -fanalyzer \
        -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" \
        -c "$tools/rtw88-pci-contract.c" -o "$work/analysis.o"
    ${CC:-cc} -std=c11 $rfk_flags -dM -E - < /dev/null > "$work/predefines.h"
    cppcheck --enable=warning,performance,portability --error-exitcode=1 \
        --check-level=exhaustive --include="$work/predefines.h" $rfk_flags \
        -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" \
        "$tools/rtw88-pci-contract.c"
    ${CLANG:-clang} ${RTW88_CLANG_FLAGS:-} --analyze -std=c11 $rfk_flags \
        -Wall -Wextra -Werror -Xanalyzer -analyzer-output=text \
        -Xanalyzer -analyzer-werror \
        -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" \
        "$tools/rtw88-pci-contract.c" -o "$work/pci.plist"
fi
