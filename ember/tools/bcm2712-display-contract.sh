#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/bcm2712-display-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
sed '/^#include /d; /^__KERNEL_RCSID(/d' \
    "$src/sys/arch/arm/broadcom/bcm2712_display.c" > "$work/core.h"
sed '/^#include /d; /^__KERNEL_RCSID(/d' \
    "$src/sys/arch/arm/broadcom/bcm2712_display_acpi.c" > "$work/adapter.h"
cp "$src/sys/arch/arm/broadcom/bcm2712_display.h" "$work/display.h"
awk '/^#define[ \t]+(VCPROP_CLK_M2MC|VCPROP_CLK_PIXEL_BVB|VCPROP_CLK_CORE|VCPROP_CLK_DISP|VCPROP_REQ_SUCCESS|VCPROPTAG_RESPONSE|VCPROPTAG_GET_CLOCKSTATE|VCPROPTAG_GET_CLOCKRATE)[ \t]/' \
    "$src/sys/arch/evbarm/rpi/vcprop.h" > "$work/properties.h"
compile()
{
    # CFLAGS is a compiler argument list supplied by the caller.
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} -I"$work" \
        "$src/ember/tools/bcm2712-display-contract.c" -o "$work/contract"
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
if [ -n "${DISPLAY_OUTPUT:-}" ]; then cp "$work/contract" "$DISPLAY_OUTPUT"; fi
if [ "${DISPLAY_COMPILE_ONLY:-0}" = 1 ]; then exit 0; fi
run
if [ "${DISPLAY_SKIP_MUTANTS:-0}" = 1 ]; then exit 0; fi
cp "$work/core.h" "$work/core-good.h"
cp "$work/adapter.h" "$work/adapter-good.h"
for mutant in version memory irq hsm clock-id reply polarity; do
    cp "$work/core-good.h" "$work/core.h"
    cp "$work/adapter-good.h" "$work/adapter.h"
    case "$mutant" in
        version) sed 's/contract_version != BCM2712_DISPLAY_VERSION/contract_version == 0/' "$work/adapter-good.h" > "$work/adapter.h" ;;
        memory) sed 's/0x107d508200/0x107d508201/' "$work/core-good.h" > "$work/core.h" ;;
        irq) sed 's/"aon", 271/"aon", 270/' "$work/core-good.h" > "$work/core.h" ;;
        hsm) sed 's/values\[1\] < BCM2712_DISPLAY_HSM_MIN_RATE/values[1] < 1/' "$work/core-good.h" > "$work/core.h" ;;
        clock-id) sed 's/VCPROP_CLK_M2MC, VCPROP_CLK_PIXEL_BVB/5, VCPROP_CLK_PIXEL_BVB/' "$work/core-good.h" > "$work/core.h" ;;
        reply) sed 's/le32toh(request\[5\]) != ids\[i\]/false/' "$work/core-good.h" > "$work/core.h" ;;
        polarity) sed 's/irq->Polarity != ACPI_ACTIVE_HIGH/false/' "$work/adapter-good.h" > "$work/adapter.h" ;;
    esac
    compile
    if (ulimit -c 0; run) >"$work/mutant.log" 2>&1; then
        echo "display contract accepted unsafe mutant: $mutant" >&2
        exit 1
    fi
    echo "BCM2712 display contract: rejected $mutant mutant"
done
