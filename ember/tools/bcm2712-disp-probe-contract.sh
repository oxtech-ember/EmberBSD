#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/bcm2712-disp-probe.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
echo "DISP probe temporary directory: $work"
for pair in clock-core:bcm2712_display_clock.c disp-native:bcm2712_display_clock_acpi.c; do
    output=${pair%%:*}; input=${pair#*:}
    sed '/^#include /d; /^__KERNEL_RCSID(/d' "$src/sys/arch/arm/broadcom/$input" > "$work/$output.h"
done
for pair in display:bcm2712_display.h clockvar:bcm2712_display_clockvar.h \
    disp-types:bcm2712_display_disp.h hsm-types:bcm2712_display_hsm.h \
    bvb-types:bcm2712_display_bvb.h core-types:bcm2712_display_core.h; do
    output=${pair%%:*}; input=${pair#*:}
    sed '/^#include /d' "$src/sys/arch/arm/broadcom/$input" > "$work/$output.h"
done
sed '/^#include /d; /^__KERNEL_RCSID(/d' \
    "$src/sys/modules/bcm2712_disp_probe/bcm2712_disp_probe.c" > "$work/disp-probe.h"
# Reuse the same wire service as the existing common/native clock contracts.
awk '/^#define LOG_MAX / { copying=1 } copying && /^static void$/ { exit } copying { print }' \
    "$src/ember/tools/bcm2712-display-clock-contract.c" > "$work/clock-firmware.h"
test -s "$work/clock-firmware.h"
awk '/^#define[ \t]+(VCPROP_CLK_M2MC|VCPROP_CLK_PIXEL_BVB|VCPROP_CLK_DISP|VCPROP_CLK_CORE|VCPROP_REQ_SUCCESS|VCPROPTAG_RESPONSE|VCPROPTAG_GET_CLOCKSTATE|VCPROPTAG_GET_CLOCKRATE|VCPROPTAG_GET_MIN_CLOCKRATE|VCPROPTAG_GET_MAX_CLOCKRATE|VCPROPTAG_SET_CLOCKSTATE|VCPROPTAG_SET_CLOCKRATE)[ \t]/' \
    "$src/sys/arch/evbarm/rpi/vcprop.h" > "$work/properties.h"
# The module must call the builtin API. It must not compile another clock engine.
awk '
    /bcm2712_disp_private_|-Dbcm2712_clock_|bcm2712_display_clock\.c|bcm2712_display_disp_acpi\.c/ { bad=1 }
    /^SRCS=/ { seen=1; if (NF != 2 || $2 != "bcm2712_disp_probe.c") bad=1 }
    END { if (!seen || bad) exit 1 }
' "$src/sys/modules/bcm2712_disp_probe/Makefile"
libfdt=$src/sys/external/bsd/libfdt/dist
for unit in fdt fdt_ro fdt_rw fdt_sw fdt_wip fdt_empty_tree fdt_strerror fdt_addresses fdt_overlay; do
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra ${CFLAGS:-} -I"$libfdt" -c "$libfdt/$unit.c" -o "$work/$unit.o"
done
compile()
{
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pthread ${CFLAGS:-} \
        -I"$work" -I"$libfdt" "$src/ember/tools/bcm2712-disp-probe-contract.c" \
        "$work/"*.o -o "$work/contract"
}
run()
{
    if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/contract"
    else "$work/contract"; fi
}
compile
if [ -n "${DISP_PROBE_OUTPUT:-}" ]; then cp "$work/contract" "$DISP_PROBE_OUTPUT"; fi
if [ -n "${DISP_PROBE_EVIDENCE:-}" ]; then
    mkdir -p "$DISP_PROBE_EVIDENCE"
    cp "$work/"*.h "$src/ember/tools/bcm2712-disp-probe-contract.c" "$DISP_PROBE_EVIDENCE/"
fi
if [ "${DISP_PROBE_COMPILE_ONLY:-0}" = 1 ]; then exit 0; fi
run
if [ "${DISP_PROBE_SKIP_MUTANTS:-0}" = 1 ]; then exit 0; fi
for unit in clock-core disp-native disp-probe; do cp "$work/$unit.h" "$work/$unit-good.h"; done
for mutant in ${DISP_PROBE_MUTANTS:-id min minimize own-off fault lease identity serialization status-query one-init owner-slot hold-retain fini pre-error}; do
    for unit in clock-core disp-native disp-probe; do cp "$work/$unit-good.h" "$work/$unit.h"; done
    target=clock-core
    case "$mutant" in
        id) from='id = VCPROP_CLK_DISP;'; to='id = VCPROP_CLK_PIXEL_BVB;' ;;
        min) from='sc.status.floor == 0'; to='false' ;;
        minimize) from='sc->profile != BCM2712_CLOCK_HSM || sc->rate_changed'; to='sc->profile == BCM2712_CLOCK_BVB || sc->rate_changed' ;;
        own-off) from='sc->profile != BCM2712_CLOCK_BVB &&'; to='sc->profile == BCM2712_CLOCK_HSM &&' ;;
        fault) from='sc->status.fault = error;'; to='sc->status.fault = 0;' ;;
        lease) from='error = bcm2712_clock_apply(sc, wanted);'; to='sc->status.leases = 0; error = bcm2712_clock_apply(sc, wanted);' ;;
        identity) target=disp-native; from='sc == NULL || sc != bcm2712_disp_owner'; to='sc == NULL' ;;
        serialization) target=disp-native; from='mutex_enter(&bcm2712_clock_lock);'; to='mutex_enter(&bcm2712_clock_lock); mutex_exit(&bcm2712_clock_lock);' ;;
        status-query) target=disp-native; from='memset(out, 0, sizeof(*out));'; to='(void)bcm2712_clock_init(&sc->core, BCM2712_CLOCK_DISP, bcm2712_clock_exchange, NULL); memset(out, 0, sizeof(*out));' ;;
        one-init) target=disp-native; from='error = RUN_ONCE(&bcm2712_clock_once, bcm2712_clock_once_init);'; to='error = RUN_ONCE(&bcm2712_clock_once, bcm2712_clock_once_init); error = bcm2712_clock_once_init();' ;;
        owner-slot) target=disp-native; from='if (bcm2712_disp_owner != NULL) {'; to='if (false) {' ;;
        hold-retain) target=disp-probe; from='ctx->stage, error, ctx->operation_error);'; to='ctx->stage, error, ctx->operation_error); return error;' ;;
        fini) target=disp-probe; from='ctx->minor != NULL ? EBUSY : 0;'; to='ctx->minor != NULL ? 0 : 0;' ;;
        pre-error) target=disp-probe; from='return operation_error;'; to='return 0;' ;;
        *) echo "Unknown DISP mutant: $mutant" >&2; exit 1 ;;
    esac
    expected=1
    case "$mutant" in fault) expected=2 ;; status-query) expected=4 ;; esac
    awk -v from="$from" -v to="$to" -v expected="$expected" '
        { p=index($0,from); if(p) { count++; $0=substr($0,1,p-1) to substr($0,p+length(from)) } print }
        END { if(count != expected) exit 1 }
    ' "$work/$target-good.h" > "$work/$target.h"
    compile > "$work/mutant-compile.log" 2>&1 || {
        cat "$work/mutant-compile.log" >&2; echo "DISP mutant failed to compile: $mutant" >&2; exit 1;
    }
    if (ulimit -c 0; run) > "$work/mutant.log" 2>&1; then
        echo "DISP mutant survived: $mutant" >&2; exit 1
    fi
    if ! grep -q "FAIL" "$work/mutant.log"; then
        cat "$work/mutant.log" >&2; echo "DISP mutant missed CHECK: $mutant" >&2; exit 1
    fi
    if [ -n "${DISP_PROBE_EVIDENCE:-}" ]; then cp "$work/mutant.log" "$DISP_PROBE_EVIDENCE/mutant-$mutant.log"; fi
    echo "DISP contract rejected mutant: $mutant"
done
