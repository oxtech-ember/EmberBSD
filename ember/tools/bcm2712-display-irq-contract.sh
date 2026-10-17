#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/bcm2712-l2-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
for pair in display:bcm2712_display.h display-core:bcm2712_display.c irq:bcm2712_display_irq.h irq-core:bcm2712_display_irq.c irq-adapter:bcm2712_display_irq_acpi.c; do
    name=${pair%%:*}
    input=${pair#*:}
    sed '/^#include /d; /^__KERNEL_RCSID(/d' "$src/sys/arch/arm/broadcom/$input" > "$work/$name.h"
done
awk '/^#define[ \t]+(VCPROP_CLK_M2MC|VCPROP_CLK_PIXEL_BVB|VCPROP_CLK_CORE|VCPROP_CLK_DISP|VCPROP_REQ_SUCCESS|VCPROPTAG_RESPONSE|VCPROPTAG_GET_CLOCKSTATE|VCPROPTAG_GET_CLOCKRATE)[ \t]/' "$src/sys/arch/evbarm/rpi/vcprop.h" > "$work/properties.h"
ulimit -c 0
# Compile the actual core and native adapter. Only hardware and kernel services are fake.
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} -I"$work" \
    "$src/ember/tools/bcm2712-display-irq-contract.c" -o "$work/contract"
if [ -n "${L2_OUTPUT:-}" ]; then cp "$work/contract" "$L2_OUTPUT"; fi
if [ -n "${L2_EVIDENCE:-}" ]; then
    mkdir -p "$L2_EVIDENCE"
    cp "$work/"*.h "$src/ember/tools/bcm2712-display-irq-contract.c" "$L2_EVIDENCE/"
fi
if [ "${L2_COMPILE_ONLY:-0}" = 1 ]; then exit 0; fi
if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/contract"; else "$work/contract"; fi
if [ "${L2_SKIP_MUTANTS:-0}" = 1 ]; then exit 0; fi
cp "$work/irq-core.h" "$work/core-good.h"
cp "$work/irq-adapter.h" "$work/adapter-good.h"
for mutation in no-ack bsc-clear unknown-source foreign-enable reserved-zero startup-unknown queued-duplicate queued-stop no-drain no-rollback self-destroy; do
    cp "$work/core-good.h" "$work/irq-core.h"
    cp "$work/adapter-good.h" "$work/irq-adapter.h"
    case "$mutation" in
        no-ack) input=core-good.h; output=irq-core.h; from='if (sc->has_clear)'; to='if (false)' ;;
        bsc-clear) input=core-good.h; output=irq-core.h; from='result.mask_status = 0x04;'; to='result.has_clear = true; result.clear = 8; result.mask_status = 0x04;' ;;
        unknown-source) input=core-good.h; output=irq-core.h; from='if ((pending & ~sc->owned) != 0)'; to='if (false)' ;;
        foreign-enable) input=core-good.h; output=irq-core.h; from='~mask & sc->declared & ~sc->owned'; to='0' ;;
        reserved-zero) input=core-good.h; output=irq-core.h; from='~mask & sc->declared & ~sc->owned'; to='~mask & ~sc->owned' ;;
        startup-unknown) input=core-good.h; output=irq-core.h; from='status & ~mask & ~sc->owned'; to='status & 0' ;;
        queued-duplicate) input=adapter-good.h; output=irq-adapter.h; from='&& !sc->queued'; to='&& true' ;;
        queued-stop) input=adapter-good.h; output=irq-adapter.h; from='if (sc->stopping)'; to='if (false)' ;;
        no-drain) input=adapter-good.h; output=irq-adapter.h; from='workqueue_wait(sc->wq, &sc->work);'; to='(void)workqueue_wait;' ;;
        no-rollback) input=adapter-good.h; output=irq-adapter.h; from='bcm2712_l2_rollback(&sc->core);'; to='(void)0;' ;;
        self-destroy) input=adapter-good.h; output=irq-adapter.h; from='if (sc->worker == curlwp)'; to='if (false)' ;;
    esac
    # Replace only the first exact site; every mutant must compile and fail at runtime.
    awk -v from="$from" -v to="$to" '
        !changed { p = index($0, from); if (p) { $0 = substr($0,1,p-1) to substr($0,p+length(from)); changed++ } }
        { print } END { if (changed != 1) exit 1 }
    ' "$work/$input" > "$work/$output"
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} -I"$work" \
        "$src/ember/tools/bcm2712-display-irq-contract.c" -o "$work/mutant"
    if ("$work/mutant") >"$work/mutant.log" 2>&1; then
        echo "Surviving L2 causal mutant: $mutation" >&2
        exit 1
    fi
    echo "L2 causal mutant rejected: $mutation"
done
