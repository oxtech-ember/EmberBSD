#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/bcm2712-hsm-probe.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
for unit in hsm hsm_acpi dvp dvp_acpi; do
    sed '/^#include /d; /^__KERNEL_RCSID(/d' \
        "$src/sys/arch/arm/broadcom/bcm2712_display_${unit}.c" > "$work/$unit.h"
done
sed '/^#include /d; /^__KERNEL_RCSID(/d' \
    "$src/sys/modules/bcm2712_hsm_probe/bcm2712_hsm_probe.c" > "$work/probe.h"
cp "$src/sys/arch/arm/broadcom/bcm2712_display_hsm.h" "$work/hsm-types.h"
cp "$src/sys/arch/arm/broadcom/bcm2712_display.h" "$work/display.h"
cp "$src/ember/tools/bcm2712-display-hsm-contract.c" "$work/hsm-services.c"
awk '/^#define[ \t]+(VCPROP_CLK_M2MC|VCPROP_CLK_PIXEL_BVB|VCPROP_CLK_CORE|VCPROP_CLK_DISP|VCPROP_REQ_SUCCESS|VCPROPTAG_RESPONSE|VCPROPTAG_GET_CLOCKSTATE|VCPROPTAG_GET_CLOCKRATE|VCPROPTAG_GET_MAX_CLOCKRATE|VCPROPTAG_SET_CLOCKSTATE|VCPROPTAG_SET_CLOCKRATE)[ \t]/' \
    "$src/sys/arch/evbarm/rpi/vcprop.h" > "$work/properties.h"
for pair in display-core:bcm2712_display.c dvp-types:bcm2712_display_dvp.h; do
    name=${pair%%:*}; input=${pair#*:}
    sed '/^#include /d; /^__KERNEL_RCSID(/d' "$src/sys/arch/arm/broadcom/$input" > "$work/$name.h"
done
mv "$work/dvp.h" "$work/dvp-core.h"
mv "$work/dvp_acpi.h" "$work/dvp-native.h"
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
for mutation in fdt-full mode-fallback selected-reset hsm-held retained-map; do
    case "$mutation" in
        fdt-full)
            from='fdt_check_full(fdt, fdt_totalsize(fdt))'; to='fdt_check_header(fdt)' ;;
        selected-reset)
            from='if ((initial.resets & (1U << 1)) != 0)'; to='if (false)' ;;
        mode-fallback)
            from=''; to='' ;;
        hsm-held)
            from='error = hsm_probe_dvp_cycle(dev, handle, mode, &dvp);'
            to='(void)bcm2712_display_hsm_release(provider, 0); error = hsm_probe_dvp_cycle(dev, handle, mode, &dvp);' ;;
        retained-map)
            from='error = hsm_probe_dvp_cycle(dev, handle, mode, &dvp);'
            to='error = hsm_probe_dvp_cycle(dev, handle, mode, &dvp); if (error != 0 && dvp.mapped) bus_space_unmap(&arm_generic_bs_tag, dvp.handle, 0x10);' ;;
    esac
    if [ "$mutation" = mode-fallback ]; then
        awk '
            previous == "\t\telse" && $0 == "\t\t\treturn EINVAL;" {
                $0 = "\t\t\tmode = HSM_PROBE_CLOCK;"; changed++
            }
            { previous = $0; print } END { if (changed != 1) exit 1 }
        ' "$work/probe-good.h" > "$work/probe.h"
    else
        awk -v from="$from" -v to="$to" '
            !changed { p = index($0, from); if (p) {
                $0 = substr($0,1,p-1) to substr($0,p+length(from)); changed++
            } }
            { print } END { if (changed != 1) exit 1 }
        ' "$work/probe-good.h" > "$work/probe.h"
    fi
    compile
    if (ulimit -c 0; run) >"$work/mutant.log" 2>&1; then
        echo "HSM/DVP probe accepted causal mutant: $mutation" >&2
        exit 1
    fi
    echo "HSM/DVP probe: rejected causal mutant $mutation"
done
