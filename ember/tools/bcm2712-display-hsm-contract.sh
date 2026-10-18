#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/bcm2712-hsm-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
for unit in clock hsm hsm_acpi; do
    input=$unit
    if [ "$unit" = hsm_acpi ]; then input=clock_acpi; fi
    sed '/^#include /d; /^__KERNEL_RCSID(/d' \
        "$src/sys/arch/arm/broadcom/bcm2712_display_${input}.c" > "$work/$unit.h"
done
sed '/^#include /d' "$src/sys/arch/arm/broadcom/bcm2712_display_bvb.h" > "$work/bvb-types.h"
{
    echo '#ifndef __CTASSERT'
    echo '#define __CTASSERT(e) _Static_assert(e, #e)'
    echo '#endif'
    echo '#include "bvb-types.h"'
    cat "$work/hsm_acpi.h"
} > "$work/native.h"
mv "$work/native.h" "$work/hsm_acpi.h"
cp "$src/sys/arch/arm/broadcom/bcm2712_display_hsm.h" "$work/hsm-types.h"
cp "$src/sys/arch/arm/broadcom/bcm2712_display_clockvar.h" "$work/clockvar.h"
cp "$src/sys/arch/arm/broadcom/bcm2712_display.h" "$work/display.h"
awk '/^#define[ \t]+(VCPROP_CLK_M2MC|VCPROP_CLK_PIXEL_BVB|VCPROP_REQ_SUCCESS|VCPROPTAG_RESPONSE|VCPROPTAG_GET_CLOCKSTATE|VCPROPTAG_GET_CLOCKRATE|VCPROPTAG_GET_MIN_CLOCKRATE|VCPROPTAG_GET_MAX_CLOCKRATE|VCPROPTAG_SET_CLOCKSTATE|VCPROPTAG_SET_CLOCKRATE)[ \t]/' \
    "$src/sys/arch/evbarm/rpi/vcprop.h" > "$work/properties.h"
compile()
{
    # CFLAGS is a caller-supplied compiler argument list.
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pthread ${CFLAGS:-} -I"$work" \
        "$src/ember/tools/bcm2712-display-hsm-contract.c" -o "$work/contract"
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
if [ -n "${HSM_OUTPUT:-}" ]; then cp "$work/contract" "$HSM_OUTPUT"; fi
if [ "${HSM_COMPILE_ONLY:-0}" = 1 ]; then exit 0; fi
run
if [ "${HSM_SKIP_MUTANTS:-0}" = 1 ]; then exit 0; fi
cp "$work/clock.h" "$work/good.h"
for mutant in set-rate rate-readback other-lease fault clock-id disable-inherited; do
    case "$mutant" in
    set-rate) sed 's/if (rate < wanted ||$/if (false \&\&/' "$work/good.h" > "$work/clock.h" ;;
    rate-readback) sed 's/if (rate < wanted || rate > maximum)/if (false)/' "$work/good.h" > "$work/clock.h" ;;
    other-lease) sed 's/(sc->status.leases \& (1U << i)) != 0/false/' "$work/good.h" > "$work/clock.h" ;;
    fault) sed 's/sc->status.fault = error;/sc->status.fault = 0;/' "$work/good.h" > "$work/clock.h" ;;
    clock-id) sed 's/le32toh(packet\[5\]) != id/false/' "$work/good.h" > "$work/clock.h" ;;
    disable-inherited) sed 's/sc->initial_state == 0 \&\& sc->state_changed/sc->status.mutation_attempted/' "$work/good.h" > "$work/clock.h" ;;
    esac
    if cmp -s "$work/good.h" "$work/clock.h"; then
        echo "HSM mutant did not change source: $mutant" >&2
        exit 1
    fi
    compile
    if (ulimit -c 0; run) >"$work/mutant.log" 2>&1; then
        echo "HSM test accepted unsafe mutant: $mutant" >&2
        exit 1
    fi
    echo "HSM contract: rejected $mutant mutant"
done
