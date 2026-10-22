#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD RTL8822CE firmware-bootstrap fake-bus runner, AI-assisted.
set -eu
case ${RTW88_RFK_ANALYZE:-0} in 0|1) ;; *) exit 2 ;; esac
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
src=${RTW88_ENABLE_SOURCE:-$(CDPATH= cd -- "$tools/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/rtw88-rfk-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
mkdir -p "$work/include/sys" "$work/include/dev" "$work/include/net80211"
printf '#include "rtw88-rfk-atomic-model.h"\n' > "$work/include/sys/atomic.h"
cp "$src/sys/net80211/_ieee80211.h" "$work/include/net80211/"
python3 "$tools/rtw88-fixed-contract.py" "$src" "$work/include"
python3 "$tools/net80211-wme-contract.py" "$src" "$work/include"
python3 "$tools/rtw88-routing-contract.py" "$src" "$work/include"
python3 "$tools/rtw88-enable-contract.py" "$src" "$work/include"
python3 "$tools/rtw88-rfk-contract.py" "$src" "$work/include"
ln -s "$src/sys/dev/ic" "$work/include/dev/ic"
printf '#include <stddef.h>\n#define KM_SLEEP 0\nvoid *kmem_zalloc(size_t,int);\nvoid kmem_free(void *,size_t);\n' > "$work/include/sys/kmem.h"
for header in sys/param.h sys/systm.h; do
    printf '#include <errno.h>\n#include "rtw88-test-kernel.h"\n' > "$work/include/$header"
done
scenarios=${RTW88_RFK_CASE:-none positive stale-start stale-gapk stale-iqk stale-dpk stale-cal stale-coef snapshot-first snapshot-second write-gapk write-dpk restore-first restore-second cancel-gapk profile-gapk cancel-iqk cancel-dpk newfw-baseline zero-init c1-spacing alloc-cancel packet-write-error packet-read-error slow-io cleanup-expiry dying-cleanup post-complete-change rf18-width rf18-path rf18-channel rf18-band rf18-region rf18-read-error detach-baseline write-rxbb cancel-rxbb quarantine-rxbb quarantine-complete}
for altq in 0 1; do
    flags=-UALTQ
    [ "$altq" = 0 ] || flags=-DALTQ
    ${CC:-cc} -std=c11 -DRTW88_RFK_EXPERIMENT -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -Wall -Werror $flags ${RTW88_TEST_CFLAGS:-} \
        -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" \
        "$tools/rtw88-rfk-contract.c" \
        "$src/sys/dev/ic/rtw88_net_policy.c" \
        "$src/sys/dev/ic/rtw88.c" \
        "$src/sys/dev/ic/rtw8822c_rfk.c" \
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
    echo "ALTQ=$altq"
    for scenario in $scenarios; do
        if ! RTW88_RFK_CASE="$scenario" run_test "$work/test" > "$work/output"; then
            cat "$work/output"
            exit 1
        fi
        cat "$work/output"
    done
    if [ "${RTW88_RFK_ANALYZE:-0}" = 1 ]; then
        units=rtw88-rfk-contract.c
        # These core units have no ALTQ policy. Analyze each actual body once.
        if [ "$altq" = 0 ]; then
            units="$units rtw8822c_rfk.c rtw88_fw_init.c rtw88.c"
        fi
        ${CC:-cc} -std=c11 -DRTW88_RFK_EXPERIMENT -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -dM -E $flags - < /dev/null > "$work/predefines.h"
        for relative in $units; do
            case $relative in *-contract.c) unit="$tools/$relative" ;; *) unit="$src/sys/dev/ic/$relative" ;; esac
            name=${relative%.c}
            ${CC:-cc} -std=c11 -DRTW88_RFK_EXPERIMENT -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -O0 -g -Wall -Werror -fanalyzer $flags \
                -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" \
                -c "$unit" -o "$work/$name-analysis.o"
            cppcheck --enable=warning,performance,portability --error-exitcode=1 \
                --check-level=exhaustive --include="$work/predefines.h" $flags -U_KERNEL -U_MODULE \
                -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" "$unit"
            ${CLANG:-clang} ${RTW88_CLANG_FLAGS:-} --analyze -std=c11 -DRTW88_RFK_EXPERIMENT -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE $flags \
                -Wall -Werror -Xanalyzer -analyzer-output=text -Xanalyzer -analyzer-werror \
                -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" \
                "$unit" -o "$work/$name.plist"
        done
    fi
done
# The experiment cannot unload its kernel-lifetime quarantine.
if ${CC:-cc} -std=c11 -DRTW88_RFK_EXPERIMENT -D_MODULE -fsyntax-only \
    -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" "$src/sys/dev/ic/rtw8822c_rfk.c" > "$work/module.log" 2>&1; then
    echo "experimental module guard failed" >&2; exit 1
fi
grep 'Experimental RFK quarantine requires a built-in' "$work/module.log"
# Also execute the actual PCI formatter and all buffer-capacity cases with the
# experimental numeric mirror present. The ordinary PCI runner stays OFF.
RTW88_PCI_RFK=1 sh "$tools/rtw88-pci-contract.sh"
