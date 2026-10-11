#!/bin/sh
# Origin: EmberBSD native V3D fault-recovery contract, 2026-10-11.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/bcm2712-v3d-fault.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
sed '/^#include /d; /^__KERNEL_RCSID(/d' \
    "$src/sys/arch/arm/broadcom/bcm2712_v3d_fault.c" > "$work/fault-body.h"
compile_contract()
{
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
        ${CFLAGS:-} -I"$work" \
        "$src/ember/tools/bcm2712-v3d-fault-contract.c" -o "$1"
}
run_contract()
{
    if [ -n "${TEST_RUNNER:-}" ]; then
        "$TEST_RUNNER" "$1"
    else
        "$1"
    fi
}
compile_contract "$work/contract"
if [ -n "${V3D_FAULT_OUTPUT:-}" ]; then
    cp "$work/contract" "$V3D_FAULT_OUTPUT"
fi
if [ "${V3D_FAULT_COMPILE_ONLY:-0}" = 1 ]; then
    exit 0
fi
run_contract "$work/contract"
if [ "${V3D_FAULT_SKIP_MUTANTS:-0}" = 1 ]; then
    exit 0
fi
cp "$work/fault-body.h" "$work/fault-good.h"
for mutation in skip-latch-clear skip-flush fixup-without-sync \
    release-always retry-faulted-job accept-mixed-outcomes \
    ignore-conversion-counter ignore-fault-identity wrong-vio-shift \
    ignore-busy ignore-scratch ignore-sources ignore-alias-destination \
    skip-pt-postwrite skip-first-ownership-return ignore-raw-status \
    skip-final-boundary milliseconds-as-ticks skip-controller-mask \
    skip-interrupt-drain; do
    cp "$work/fault-good.h" "$work/fault-body.h"
    awk -v mutation="$mutation" '
        mutation == "skip-latch-clear" &&
        /error = bcmv3d_takeover_hub_poke\(FV3D_MMU_CTL, value\);/ {
            print "\terror = 0;"
            changed = 1
            next
        }
        mutation == "skip-flush" {
            changed += gsub(/FV3D_MMUC_ENABLE \| FV3D_MMUC_FLUSH/,
                "FV3D_MMUC_ENABLE")
        }
        mutation == "fixup-without-sync" && /fv3d_sync_all\(true\);/ {
            if (++presyncs == 2) {
                changed = 1
                next
            }
        }
        mutation == "accept-mixed-outcomes" {
            changed += sub(/if \(\(atomic_load_relaxed\(&fv3d.delivered\) & FV3D_HUB_TFUC\) != 0\)/,
                "if ((atomic_load_relaxed(\\&fv3d.delivered) \\& FV3D_HUB_TFUC) != 0 \\&\\& false)")
            changed += sub(/atomic_load_relaxed\(&fv3d.tfuc_events\) != \(completed \? 1u : 0u\)/,
                "(completed \\&\\& atomic_load_relaxed(\\&fv3d.tfuc_events) != 1u)")
        }
        mutation == "ignore-conversion-counter" {
            changed += sub(/\(uint8_t\)\(\(cs >>/, "((uint8_t)((cs >>")
            changed += sub(/!= \(completed \? 1 : 0\)\) \{/,
                "!= (completed ? 1 : 0) \\&\\& false)) {")
        }
        mutation == "ignore-fault-identity" {
            changed += sub(/if \(\(vio_id >> 5\) != 5 \|\| fault_va < ldst \|\|/,
                "if (false \\&\\& ((vio_id >> 5) != 5 || fault_va < ldst ||")
            changed += sub(/fault_va >= \(uint64_t\)ldst \+ FV3D_IMAGE_SIZE\) \{/,
                "fault_va >= (uint64_t)ldst + FV3D_IMAGE_SIZE)) {")
        }
        mutation == "wrong-vio-shift" {
            changed += sub(/fv3d.va_width - 32/, "0")
        }
        mutation == "ignore-busy" {
            changed += gsub(/& FV3D_TFU_BUSY/, "\\& (FV3D_TFU_BUSY \\& 0)")
        }
        mutation == "ignore-scratch" {
            changed += sub(/if \(image\[word\] != expected\)/,
                "if (image[word] != expected \\&\\& id != FV3D_OBJ_SCRATCH)")
        }
        mutation == "ignore-sources" {
            changed += sub(/if \(image\[word\] != expected\)/,
                "if (image[word] != expected \\&\\& id != FV3D_OBJ_TFU_ASRC \\&\\& id != FV3D_OBJ_TFU_LSRC)")
        }
        mutation == "ignore-alias-destination" {
            changed += sub(/if \(image\[word\] != expected\)/,
                "if (image[word] != expected \\&\\& id != FV3D_OBJ_TFU_LDST)")
        }
        mutation == "skip-pt-postwrite" {
            changed += sub(/ops = before \? BUS_DMASYNC_PREWRITE : BUS_DMASYNC_POSTWRITE;/,
                "ops = before ? BUS_DMASYNC_PREWRITE : 0;")
        }
        mutation == "skip-first-ownership-return" && /fv3d_sync_all\(false\);/ {
            if (++postsyncs == 1) {
                changed = 1
                next
            }
        }
        mutation == "ignore-raw-status" {
            changed += sub(/if \(status != 0 \|\|/,
                "if ((status != 0 \\&\\& false) ||")
        }
        mutation == "skip-final-boundary" &&
        /fv3d.stage = "final masked acceptance boundary";/ {
            skip_final = 1
        }
        mutation == "skip-final-boundary" && skip_final &&
        /error = fv3d_check_event\(true, NULL, NULL\);/ {
            changed = 1
            next
        }
        mutation == "milliseconds-as-ticks" {
            changed += sub(/MAX\(1, mstohz\(FV3D_WAIT_SLICE_MS\)\)/,
                "FV3D_WAIT_SLICE_MS")
        }
        mutation == "skip-controller-mask" && /acpi_intr_mask\(fv3d.hub_ih\);/ {
            print "\t\tif (false) acpi_intr_mask(fv3d.hub_ih);"
            changed = 1
            next
        }
        mutation == "skip-interrupt-drain" && /xc_barrier\(0\);/ {
            print "\t\tif (false) xc_barrier(0);"
            changed = 1
            next
        }
        mutation == "release-always" {

            changed += sub(/if \(!fv3d\.published\)/, "if (1)")
        }
        mutation == "retry-faulted-job" &&
        /fv3d.stage = "valid destination alias";/ {
            print "\t(void)bcmv3d_takeover_hub_poke(FV3D_TFU_ICFG, FV3D_TFU_ICFG_VALUE);"
            changed = 1
        }
        { print }
        END { exit changed > 0 ? 0 : 1 }
    ' "$work/fault-good.h" > "$work/fault-body.h" || {
        echo "mutant $mutation: source pattern not found" >&2
        exit 1
    }
    if ! compile_contract "$work/contract-mutant"; then
        echo "mutant $mutation failed to compile" >&2
        exit 1
    fi
    if run_contract "$work/contract-mutant" >/dev/null 2>&1; then
        echo "mutant $mutation was not rejected" >&2
        exit 1
    fi
done
echo "PASS: fault contract; 20 causal mutants rejected"
