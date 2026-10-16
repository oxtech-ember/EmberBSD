#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/bcm2712-dvp-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
for pair in display:bcm2712_display.h display-core:bcm2712_display.c dvp:bcm2712_display_dvp.h dvp-core:bcm2712_display_dvp.c dvp-native:bcm2712_display_dvp_acpi.c; do
    name=${pair%%:*}
    input=${pair#*:}
    sed '/^#include /d; /^__KERNEL_RCSID(/d' "$src/sys/arch/arm/broadcom/$input" > "$work/$name.h"
done
awk '/^#define[ \t]+(VCPROP_CLK_M2MC|VCPROP_CLK_PIXEL_BVB|VCPROP_CLK_CORE|VCPROP_CLK_DISP|VCPROP_REQ_SUCCESS|VCPROPTAG_RESPONSE|VCPROPTAG_GET_CLOCKSTATE|VCPROPTAG_GET_CLOCKRATE)[ \t]/' "$src/sys/arch/evbarm/rpi/vcprop.h" > "$work/properties.h"
ulimit -c 0
compile()
{
    # CFLAGS is a caller-supplied compiler argument list.
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pthread ${CFLAGS:-} -I"$work" \
        "$src/ember/tools/bcm2712-display-dvp-contract.c" -o "$work/contract"
}
run()
{
    if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/contract"; else "$work/contract"; fi
}
compile
if [ -n "${DVP_OUTPUT:-}" ]; then cp "$work/contract" "$DVP_OUTPUT"; fi
if [ -n "${DVP_EVIDENCE:-}" ]; then
    mkdir -p "$DVP_EVIDENCE"
    cp "$work/"*.h "$src/ember/tools/bcm2712-display-dvp-contract.c" "$DVP_EVIDENCE/"
fi
if [ "${DVP_COMPILE_ONLY:-0}" = 1 ]; then exit 0; fi
run
if [ "${DVP_SKIP_MUTANTS:-0}" = 1 ]; then exit 0; fi
cp "$work/dvp-core.h" "$work/core-good.h"
cp "$work/dvp-native.h" "$work/native-good.h"
for mutation in gate-index acquire-polarity release-polarity foreign-bits reset-state readback barrier fault release-lease active-destroy inherited-gate singleton serialization; do
    cp "$work/core-good.h" "$work/dvp-core.h"
    cp "$work/native-good.h" "$work/dvp-native.h"
    input=core-good.h; output=dvp-core.h
    case "$mutation" in
        gate-index) from='return 1U << (port + 3);'; to='return 1U << (port + 4);' ;;
        acquire-polarity) from='gates & ~bit, &gates'; to='gates | bit, &gates' ;;
        release-polarity) from='gates | bit, &gates'; to='gates & ~bit, &gates' ;;
        foreign-bits) from='gates & ~bit, &gates'; to='0, &gates' ;;
        reset-state) from='if ((resets & bcm2712_dvp_reset(port)) != 0)'; to='if (false)' ;;
        readback) from='if (g != wanted || (r & bcm2712_dvp_reset(port)) != 0)'; to='if (false)' ;;
        barrier) from='sc->io.barrier(sc->io.arg);'; to='(void)0;' ;;
        fault) from='sc->status.fault = error;'; to='sc->status.fault = 0;' ;;
        release-lease) from='sc->status.leases &= ~(1U << port);'; to='sc->status.leases = 0;' ;;
        active-destroy) from='if (sc->status.leases != 0)'; to='if (false)' ;;
        inherited-gate) from='if ((sc->restore & bit) != 0)'; to='if (true)' ;;
        singleton) input=native-good.h; output=dvp-native.h; from='if (bcm2712_dvp_owner != NULL)'; to='if (false)' ;;
        serialization) input=native-good.h; output=dvp-native.h; from='mutex_enter(&bcm2712_dvp_lock);'; to='(void)mutex_enter;' ;;
    esac
    awk -v from="$from" -v to="$to" '
        !changed { p = index($0, from); if (p) { $0 = substr($0,1,p-1) to substr($0,p+length(from)); changed++ } }
        { print } END { if (changed != 1) exit 1 }
    ' "$work/$input" > "$work/$output"
    compile
    if (run) >"$work/mutant.log" 2>&1; then
        echo "Surviving DVP causal mutant: $mutation" >&2
        exit 1
    fi
    echo "DVP causal mutant rejected: $mutation"
done
