#!/bin/sh
# Origin: EmberBSD passive BCM2712 V3D observation contract, 2026-10-09.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/bcm2712-v3d-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
sed '/^#include /d; /^__KERNEL_RCSID(/d' \
    "$src/sys/arch/arm/broadcom/bcm2712_v3d_acpi.c" > "$work/driver.h"
awk '/^#define[ \t]+(VCPROP_CLK_V3D|VCPROP_REQ_SUCCESS|VCPROPTAG_RESPONSE|VCPROPTAG_GET_CLOCKSTATE|VCPROPTAG_GET_CLOCKRATE|VCPROPTAG_GET_CLOCK_MEASURED)[ \t]/' \
    "$src/sys/arch/evbarm/rpi/vcprop.h" > "$work/properties.h"
awk '/^#define BCM2712_PM_GRAFX[ \t]/' \
    "$src/sys/arch/arm/broadcom/bcm2835_pmwdog_acpi.c" >> "$work/properties.h"
printf 'int\n' > "$work/pm-accessor.h"
sed -n '/^bcmpmwdog_v3d_status(/,/^}/p' \
    "$src/sys/arch/arm/broadcom/bcm2835_pmwdog_acpi.c" >> "$work/pm-accessor.h"
compile_contract()
{
    output=$1
    shift
    # Compile the production driver and PM accessor; only services are fake.
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
        ${CFLAGS:-} "$@" -I"$work" \
        "$src/ember/tools/bcm2712-v3d-contract.c" -o "$output"
}
compile_contract "$work/contract"
if [ -n "${V3D_OUTPUT:-}" ]; then
    cp "$work/contract" "$V3D_OUTPUT"
fi
run_contract()
{
    if [ -n "${TEST_RUNNER:-}" ]; then
        "$TEST_RUNNER" "$1"
    else
        "$1"
    fi
}
if [ "${V3D_COMPILE_ONLY:-0}" = 1 ]; then
    exit 0
fi
run_contract "$work/contract"
for experiment in takeover dma queue irq fault render; do
    case "$experiment" in
        takeover) probe_flags= ;;
        dma) probe_flags=-DBCM2712_V3D_DMA_PROBE ;;
        queue) probe_flags='-DBCM2712_V3D_DMA_PROBE -DBCM2712_V3D_QUEUE_PROBE' ;;
        irq) probe_flags=-DBCM2712_V3D_IRQ_PROBE ;;
        fault) probe_flags=-DBCM2712_V3D_FAULT_PROBE ;;
        render) probe_flags=-DBCM2712_V3D_RENDER ;;
    esac
    # shellcheck disable=SC2086
    compile_contract "$work/contract-$experiment" \
        -DBCM2712_V3D_TAKEOVER $probe_flags
    run_contract "$work/contract-$experiment"
    echo "BCM2712 V3D finalizer $experiment: failure propagation and one-shot PASS"
done
reject_config()
{
    invalid=$1
    diagnostic=$2
    shift 2
    if compile_contract "$work/invalid" "$@" >"$work/invalid.log" 2>&1; then
        echo "unsafe V3D configuration compiled: $invalid" >&2
        exit 1
    fi
    if ! grep -F "$diagnostic" "$work/invalid.log" >/dev/null; then
        cat "$work/invalid.log" >&2
        echo "missing expected V3D configuration diagnostic: $invalid" >&2
        exit 1
    fi
    echo "BCM2712 V3D unsafe configuration rejected: $invalid"
}
for probe in DMA QUEUE IRQ FAULT; do
    reject_config "render with $probe" 'render and boot probes cannot share one boot' \
        -DBCM2712_V3D_TAKEOVER -DBCM2712_V3D_RENDER \
        "-DBCM2712_V3D_${probe}_PROBE"
    reject_config "$probe without takeover" 'experiments require BCM2712_V3D_TAKEOVER' \
        "-DBCM2712_V3D_${probe}_PROBE"
done
reject_config 'render without takeover' 'render requires BCM2712_V3D_TAKEOVER' \
    -DBCM2712_V3D_RENDER
reject_config 'queue without DMA' 'QUEUE_PROBE requires BCM2712_V3D_DMA_PROBE' \
    -DBCM2712_V3D_TAKEOVER -DBCM2712_V3D_QUEUE_PROBE
for probe in IRQ FAULT; do
    reject_config "$probe with DMA" 'polling and interrupt experiments cannot share one boot' \
        -DBCM2712_V3D_TAKEOVER -DBCM2712_V3D_DMA_PROBE \
        "-DBCM2712_V3D_${probe}_PROBE"
    reject_config "$probe with queue" 'polling and interrupt experiments cannot share one boot' \
        -DBCM2712_V3D_TAKEOVER -DBCM2712_V3D_DMA_PROBE \
        -DBCM2712_V3D_QUEUE_PROBE "-DBCM2712_V3D_${probe}_PROBE"
done
reject_config 'IRQ with fault' 'IRQ and fault experiments cannot share one boot' \
    -DBCM2712_V3D_TAKEOVER -DBCM2712_V3D_IRQ_PROBE -DBCM2712_V3D_FAULT_PROBE
if [ "${V3D_SKIP_MUTANTS:-0}" = 1 ]; then
    exit 0
fi
cp "$work/driver.h" "$work/driver-good.h"
for mutation in clock-state clock-rate pm-reset echoed-id sms-state \
    sms-flags sms-modes sms-stable cleanup; do
    awk -v mutation="$mutation" '
        mutation == "clock-state" {
            changed += sub(/sc->sc_clock_state.value != 1/, "false")
        }
        mutation == "clock-rate" {
            changed += sub(/sc->sc_clock_rate.value == 0/, "false")
        }
        mutation == "pm-reset" {
            changed += sub(/\(sc->sc_pm & BCMV3D_RESET_N\) == 0/, "false")
        }
        mutation == "echoed-id" {
            changed += sub(/le32toh\(request\[5\]\) != VCPROP_CLK_V3D/, "false")
        }
        mutation == "sms-state" {
            changed += sub(/\(value & BCMV3D_SMS_STATE_MASK\) == 0/, "true")
        }
        mutation == "sms-flags" {
            changed += sub(/\(value & ~allowed\) == 0/, "(& || true)")
        }
        mutation == "sms-modes" {
            changed += sub(/old_mode == new_mode/, "(old_mode == new_mode || true)")
        }
        mutation == "sms-stable" {
            changed += sub(/sms\[i\] != sc->sc_sms\[i\]/, "false")
        }
        mutation == "cleanup" {
            changed += sub(/if \(mapped\[i\]\)/, "if (!mapped[i])")
        }
        { print }
        END { if (changed != 1) exit 1 }
    ' "$work/driver-good.h" > "$work/driver.h"
    # Each mutant must compile and then fail a behavioral assertion.
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
        ${CFLAGS:-} -I"$work" "$src/ember/tools/bcm2712-v3d-contract.c" \
        -o "$work/mutant"
    if (ulimit -c 0; run_contract "$work/mutant") > "$work/mutant.log" 2>&1; then
        echo "surviving BCM2712 V3D causal mutant: $mutation" >&2
        exit 1
    fi
    echo "BCM2712 V3D causal mutant rejected: $mutation"
done
sed 's/error = bcmv3d_dma_probe/(void)bcmv3d_dma_probe/' \
    "$work/driver-good.h" > "$work/driver.h"
compile_contract "$work/finalizer-mutant" -DBCM2712_V3D_TAKEOVER \
    -DBCM2712_V3D_DMA_PROBE -DBCM2712_V3D_QUEUE_PROBE
if (ulimit -c 0; run_contract "$work/finalizer-mutant") >"$work/mutant.log" 2>&1; then
    echo 'surviving BCM2712 V3D causal mutant: ignore DMA failure' >&2
    exit 1
fi
echo 'BCM2712 V3D causal mutant rejected: ignore DMA failure'
