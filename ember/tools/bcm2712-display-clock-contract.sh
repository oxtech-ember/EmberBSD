#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/bcm2712-clock-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
for unit in clock hsm; do
    sed '/^#include /d; /^__KERNEL_RCSID(/d' \
        "$src/sys/arch/arm/broadcom/bcm2712_display_${unit}.c" > "$work/$unit.h"
done
mv "$work/clock.h" "$work/clock-core.h"
reference=$src/ember/tools/fixtures/bcm2712-hsm-reference.c
expected=8c07499213bc7e1dcba43ddce092618994edc90fd3722de121afad9079223bb4
if command -v sha256 >/dev/null 2>&1; then
    actual=$(sha256 -q "$reference")
elif command -v sha256sum >/dev/null 2>&1; then
    actual=$(sha256sum "$reference")
elif command -v shasum >/dev/null 2>&1; then
    actual=$(shasum -a 256 "$reference")
else
    echo "The clock contract requires sha256, sha256sum or shasum" >&2
    exit 1
fi
if [ "${actual%% *}" != "$expected" ]; then
    echo "The frozen BSD HSM reference hash does not match" >&2
    exit 1
fi
sed '/^#include /d; /^__KERNEL_RCSID(/d' "$reference" > "$work/hsm-reference.h"
cp "$src/sys/arch/arm/broadcom/bcm2712_display_clockvar.h" "$work/clockvar.h"
cp "$src/sys/arch/arm/broadcom/bcm2712_display_hsm.h" "$work/hsm-types.h"
cp "$src/sys/arch/arm/broadcom/bcm2712_display.h" "$work/display.h"
awk '/^#define[ \t]+(VCPROP_CLK_M2MC|VCPROP_CLK_PIXEL_BVB|VCPROP_REQ_SUCCESS|VCPROPTAG_RESPONSE|VCPROPTAG_GET_CLOCKSTATE|VCPROPTAG_GET_CLOCKRATE|VCPROPTAG_GET_MIN_CLOCKRATE|VCPROPTAG_GET_MAX_CLOCKRATE|VCPROPTAG_SET_CLOCKSTATE|VCPROPTAG_SET_CLOCKRATE)[ \t]/' \
    "$src/sys/arch/evbarm/rpi/vcprop.h" > "$work/properties.h"
compile()
{
    # CFLAGS is a caller-supplied compiler argument list.
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} -I"$work" \
        "$src/ember/tools/bcm2712-display-clock-contract.c" -o "$work/contract"
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
if [ -n "${CLOCK_OUTPUT:-}" ]; then cp "$work/contract" "$CLOCK_OUTPUT"; fi
if [ "${CLOCK_COMPILE_ONLY:-0}" = 1 ]; then exit 0; fi
run
if [ "${CLOCK_SKIP_MUTANTS:-0}" = 1 ]; then exit 0; fi
cp "$work/clock-core.h" "$work/good.h"
cp "$work/hsm.h" "$work/hsm-good.h"
for mutant in ${CLOCK_MUTANTS:-clock-id hsm-trace bvb-floor bvb-inherited bvb-off
    min-zero other-lease rate-response rate-readback state-readback fault early-lease
    facade-fault facade-floor facade-flags facade-ready}; do
    cp "$work/good.h" "$work/clock-core.h"
    cp "$work/hsm-good.h" "$work/hsm.h"
    target=clock-core.h
    case "$mutant" in
    clock-id) sed 's/le32toh(packet\[5\]) != id/false/' "$work/good.h" > "$work/clock-core.h" ;;
    hsm-trace) sed -e 's/VCPROPTAG_GET_CLOCKSTATE, 0, state/VCPROPTAG_GET_CLOCKRATE, 0, rate/' -e t -e 's/VCPROPTAG_GET_CLOCKRATE, 0, rate/VCPROPTAG_GET_CLOCKSTATE, 0, state/' "$work/good.h" > "$work/clock-core.h" ;;
    bvb-floor) sed 's/sc.initial_state = sc.status.state;/sc.status.floor = MAX(sc.status.floor, sc.status.rate); sc.initial_state = sc.status.state;/' "$work/good.h" > "$work/clock-core.h" ;;
    bvb-inherited) sed 's/sc->profile == BCM2712_CLOCK_BVB || sc->rate_changed/sc->rate_changed/' "$work/good.h" > "$work/clock-core.h" ;;
    bvb-off) sed 's/sc->profile == BCM2712_CLOCK_HSM \&\&/true \&\&/' "$work/good.h" > "$work/clock-core.h" ;;
    min-zero) sed 's/sc.status.floor == 0/false/' "$work/good.h" > "$work/clock-core.h" ;;
    other-lease) sed 's/(sc->status.leases \& (1U << i)) != 0/false/' "$work/good.h" > "$work/clock-core.h" ;;
    rate-response) sed 's/reply < wanted || reply > maximum/false/' "$work/good.h" > "$work/clock-core.h" ;;
    rate-readback) sed 's/rate < wanted || rate > maximum/false/' "$work/good.h" > "$work/clock-core.h" ;;
    state-readback) sed 's/state != 1 || rate < wanted || rate > maximum/false/' "$work/good.h" > "$work/clock-core.h" ;;
    fault) sed 's/sc->status.fault = error;/sc->status.fault = 0;/' "$work/good.h" > "$work/clock-core.h" ;;
    early-lease) sed 's/error = bcm2712_clock_apply(sc, wanted);/sc->status.minimum[port] = minimum; sc->status.leases |= 1U << port; error = bcm2712_clock_apply(sc, wanted);/' "$work/good.h" > "$work/clock-core.h" ;;
    facade-fault) target=hsm.h; sed 's/sc->status.fault = clock->status.fault;/sc->status.fault = 0;/' "$work/hsm-good.h" > "$work/hsm.h" ;;
    facade-floor) target=hsm.h; sed 's/clock->status.floor = sc->floor;/clock->status.floor = BCM2712_DISPLAY_HSM_MIN_RATE;/' "$work/hsm-good.h" > "$work/hsm.h" ;;
    facade-flags) target=hsm.h; sed 's/sc->state_changed = clock->state_changed;/sc->state_changed = false;/' "$work/hsm-good.h" > "$work/hsm.h" ;;
    facade-ready) target=hsm.h; sed 's/sc == NULL || !sc->initialized/false/' "$work/hsm-good.h" > "$work/hsm.h" ;;
    *) echo "Unknown clock mutant: $mutant" >&2; exit 1 ;;
    esac
    original=good.h
    if [ "$target" = hsm.h ]; then original=hsm-good.h; fi
    if cmp -s "$work/$original" "$work/$target"; then
        echo "Clock mutant did not change source: $mutant" >&2
        exit 1
    fi
    compile
    if (ulimit -c 0; run) >"$work/mutant.log" 2>&1; then
        echo "Clock test accepted unsafe mutant: $mutant" >&2
        exit 1
    fi
    echo "Clock contract: rejected $mutant mutant"
done
