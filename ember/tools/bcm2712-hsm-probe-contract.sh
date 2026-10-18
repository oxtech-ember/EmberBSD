#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/bcm2712-hsm-probe.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
for unit in hsm hsm_acpi; do
    sed '/^#include /d; /^__KERNEL_RCSID(/d' \
        "$src/sys/arch/arm/broadcom/bcm2712_display_${unit}.c" > "$work/$unit.h"
done
sed '/^#include /d; /^__KERNEL_RCSID(/d' \
    "$src/sys/modules/bcm2712_hsm_probe/bcm2712_hsm_probe.c" > "$work/probe.h"
cp "$src/sys/arch/arm/broadcom/bcm2712_display_hsm.h" "$work/hsm-types.h"
cp "$src/sys/arch/arm/broadcom/bcm2712_display.h" "$work/display.h"
cp "$src/ember/tools/bcm2712-display-hsm-contract.c" "$work/hsm-services.c"
awk '/^#define[ \t]+(VCPROP_CLK_M2MC|VCPROP_REQ_SUCCESS|VCPROPTAG_RESPONSE|VCPROPTAG_GET_CLOCKSTATE|VCPROPTAG_GET_CLOCKRATE|VCPROPTAG_GET_MAX_CLOCKRATE|VCPROPTAG_SET_CLOCKSTATE|VCPROPTAG_SET_CLOCKRATE)[ \t]/' \
    "$src/sys/arch/evbarm/rpi/vcprop.h" > "$work/properties.h"
libfdt=$src/sys/external/bsd/libfdt/dist
# Keep diagnostics from unchanged upstream libfdt. Apply the fixture's strict
# warning policy to the new test and included production probe, not to libfdt.
for unit in fdt fdt_ro fdt_rw fdt_sw fdt_wip fdt_empty_tree fdt_strerror \
    fdt_addresses fdt_overlay; do
    # CFLAGS is a caller-supplied argument list.
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra ${CFLAGS:-} -I"$libfdt" \
        -c "$libfdt/$unit.c" -o "$work/$unit.o"
done
compile()
{
    set -- "$work"/*.o
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pthread ${CFLAGS:-} \
        -I"$work" -I"$libfdt" \
        "$src/ember/tools/bcm2712-hsm-probe-contract.c" "$@" -o "$work/contract"
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
if [ -n "${HSM_PROBE_OUTPUT:-}" ]; then cp "$work/contract" "$HSM_PROBE_OUTPUT"; fi
if [ "${HSM_PROBE_COMPILE_ONLY:-0}" = 1 ]; then exit 0; fi
run
if [ "${HSM_PROBE_SKIP_MUTANT:-0}" = 1 ]; then exit 0; fi
cp "$work/probe.h" "$work/probe-good.h"
sed 's/fdt_check_full(fdt, fdt_totalsize(fdt))/fdt_check_header(fdt)/' \
    "$work/probe-good.h" > "$work/probe.h"
compile
if (ulimit -c 0; run) >"$work/mutant.log" 2>&1; then
    echo 'HSM probe accepted header-only FDT validation' >&2
    exit 1
fi
echo 'HSM probe: rejected actual-libfdt header-only mutant'
