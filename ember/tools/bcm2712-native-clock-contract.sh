#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/bcm2712-native-clock.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
echo "Native clock temporary directory: $work"
for pair in clock-core:bcm2712_display_clock.c native-clock:bcm2712_display_clock_acpi.c; do
    output=${pair%%:*}; input=${pair#*:}
    sed '/^#include /d; /^__KERNEL_RCSID(/d' \
        "$src/sys/arch/arm/broadcom/$input" > "$work/$output.h"
done
for pair in display:bcm2712_display.h clockvar:bcm2712_display_clockvar.h \
    hsm-types:bcm2712_display_hsm.h acpi-types:bcm2712_display_acpi.h \
    bvb-types:bcm2712_display_bvb.h; do
    output=${pair%%:*}; input=${pair#*:}
    sed '/^#include /d' "$src/sys/arch/arm/broadcom/$input" > "$work/$output.h"
done
awk '/^#define LOG_MAX / { copying=1 } copying && /^static void$/ { exit } copying { print }' \
    "$src/ember/tools/bcm2712-display-clock-contract.c" > "$work/clock-firmware.h"
test -s "$work/clock-firmware.h"
awk '/^#define[ \t]+(VCPROP_CLK_M2MC|VCPROP_CLK_PIXEL_BVB|VCPROP_CLK_DISP|VCPROP_REQ_SUCCESS|VCPROPTAG_RESPONSE|VCPROPTAG_GET_CLOCKSTATE|VCPROPTAG_GET_CLOCKRATE|VCPROPTAG_GET_MIN_CLOCKRATE|VCPROPTAG_GET_MAX_CLOCKRATE|VCPROPTAG_SET_CLOCKSTATE|VCPROPTAG_SET_CLOCKRATE)[ \t]/' \
    "$src/sys/arch/evbarm/rpi/vcprop.h" > "$work/properties.h"
compile()
{
    # CFLAGS is a caller-supplied compiler argument list.
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pthread ${CFLAGS:-} -I"$work" \
        "$src/ember/tools/bcm2712-native-clock-contract.c" -o "$work/contract"
}
run()
{
    if [ -n "${TEST_RUNNER:-}" ]; then
        "$TEST_RUNNER" "$work/contract"
    else
        "$work/contract"
    fi
}
compile
if [ -n "${NATIVE_CLOCK_OUTPUT:-}" ]; then cp "$work/contract" "$NATIVE_CLOCK_OUTPUT"; fi
if [ -n "${NATIVE_CLOCK_EVIDENCE:-}" ]; then
    mkdir -p "$NATIVE_CLOCK_EVIDENCE"
    cp "$work/"*.h "$src/ember/tools/bcm2712-native-clock-contract.c" "$NATIVE_CLOCK_EVIDENCE/"
fi
if [ "${NATIVE_CLOCK_COMPILE_ONLY:-0}" = 1 ]; then exit 0; fi
run
if [ "${NATIVE_CLOCK_SKIP_MUTANTS:-0}" = 1 ]; then exit 0; fi
cp "$work/native-clock.h" "$work/native-good.h"
cp "$work/clock-core.h" "$work/core-good.h"
for mutant in ${NATIVE_CLOCK_MUTANTS:-irq-context hsm-slot bvb-slot hsm-identity bvb-identity serialization bvb-profile status-padding status-query hsm-status bvb-floor retention bvb-retention bvb-disable}; do
    cp "$work/native-good.h" "$work/native-clock.h"
    cp "$work/core-good.h" "$work/clock-core.h"
    target=native-clock
    occurrence=1
    case "$mutant" in
        irq-context) from='if (cpu_intr_p() || cpu_softintr_p())'; to='if (false)' ;;
        hsm-slot) from='if (bcm2712_hsm_owner != NULL)'; to='if (false)' ;;
        bvb-slot) from='if (bcm2712_bvb_owner != NULL)'; to='if (false)' ;;
        hsm-identity) from='sc == NULL || sc != bcm2712_hsm_owner'; to='sc == NULL' ;;
        bvb-identity) from='sc == NULL || sc != bcm2712_bvb_owner'; to='sc == NULL' ;;
        serialization) from='mutex_enter(&bcm2712_clock_lock);'; to='mutex_enter(&bcm2712_clock_lock); mutex_exit(&bcm2712_clock_lock);' ;;
        bvb-profile) from='BCM2712_CLOCK_BVB,'; to='BCM2712_CLOCK_HSM,' ;;
        status-padding) from='memset(out, 0, sizeof(*out));'; to='/* missing output initialization */' ;;
        status-query) from='memset(out, 0, sizeof(*out));'; to='(void)bcm2712_clock_init(&sc->core, BCM2712_CLOCK_HSM, bcm2712_clock_exchange, NULL); memset(out, 0, sizeof(*out));' ;;
        hsm-status) from='out->fault = sc->core.status.fault;'; to='out->fault = 0;' ;;
        bvb-floor) from='out->floor = sc->core.status.floor;'; to='out->floor = 0;' ;;
        retention) from='if (error == 0) {'; to='if (true) {' ;;
        bvb-retention) from='if (error == 0) {'; to='if (true) {'; occurrence=2 ;;
        bvb-disable)
            target=clock-core
            from='sc->profile != BCM2712_CLOCK_BVB &&'; to='true &&' ;;
        *) echo "Unknown native clock mutant: $mutant" >&2; exit 1 ;;
    esac
    awk -v from="$from" -v to="$to" -v occurrence="$occurrence" '
        !changed { p=index($0,from); if (p && ++matched == occurrence) {
            $0=substr($0,1,p-1) to substr($0,p+length(from)); changed++
        } }
        { print } END { if (changed != 1) exit 1 }
    ' "$work/$target.h" > "$work/mutated.h"
    mv "$work/mutated.h" "$work/$target.h"
    compile
    if (ulimit -c 0; run) >"$work/mutant.log" 2>&1; then
        echo "Native clock contract accepted mutant: $mutant" >&2
        exit 1
    fi
    if [ -n "${NATIVE_CLOCK_EVIDENCE:-}" ]; then
        cp "$work/mutant.log" "$NATIVE_CLOCK_EVIDENCE/mutant-$mutant.log"
    fi
    echo "Native clock contract: rejected $mutant mutant"
done
