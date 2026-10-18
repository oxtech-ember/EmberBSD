#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/bcm2712-hdmi0-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
echo "HDMI0 timing temporary directory: $work"
sed '/^#include /d; /^__KERNEL_RCSID(/d' \
    "$src/sys/arch/arm/broadcom/bcm2712_display_hdmi0.c" > "$work/hdmi0.h"
sed '/^#include /d' "$src/sys/arch/arm/broadcom/bcm2712_display_hdmi0var.h" > "$work/hdmi0var.h"
compile()
{
    # CFLAGS is a caller-supplied argument list.
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} -I"$work" \
        "$src/ember/tools/bcm2712-hdmi0-timing-contract.c" -o "$work/contract"
}
run()
{
    if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/contract"; else "$work/contract"; fi
}
compile
if [ -n "${HDMI0_OUTPUT:-}" ]; then cp "$work/contract" "$HDMI0_OUTPUT"; fi
if [ -n "${HDMI0_EVIDENCE:-}" ]; then
    mkdir -p "$HDMI0_EVIDENCE"
    cp "$work/"*.h "$src/ember/tools/bcm2712-hdmi0-timing-contract.c" "$HDMI0_EVIDENCE/"
fi
if [ "${HDMI0_COMPILE_ONLY:-0}" = 1 ]; then exit 0; fi
run
if [ "${HDMI0_SKIP_MUTANTS:-0}" = 1 ]; then exit 0; fi
cp "$work/hdmi0.h" "$work/good.h"
for mutant in ${HDMI0_MUTANTS:-horza polarity horzb verta0 verta1 vertb0 vertb1 scheduler packing-mask packing-phase gcp-mask gcp-word gcp-enable repetition clock-stop null}; do
    case "$mutant" in
        horza) from='88U << 16'; to='44U << 16' ;;
        polarity) from='(1U << 15) | (1U << 14)'; to='(1U << 14) | (1U << 13)' ;;
        horzb) from='148U << 16'; to='148U << 15' ;;
        verta0) from='.verta0 = (5U << 24)'; to='.verta0 = (5U << 20)' ;;
        verta1) from='.verta1 = (5U << 24)'; to='.verta1 = (5U << 20)' ;;
        vertb0) from='.vertb0 = 36U'; to='.vertb0 = 37U' ;;
        vertb1) from='((2200U / 2U) << 16)'; to='0U' ;;
        scheduler) from='.set_bits = (1U << 15) | (1U << 5)'; to='.set_bits = (1U << 15) | 1U' ;;
        packing-mask) from='(7U << 8) | 15U'; to='(7U << 8)' ;;
        packing-phase) from='2U << 8'; to='1U << 8' ;;
        gcp-mask) from='0xffffU'; to='0xffU' ;;
        gcp-word) from='1U << 4'; to='1U << 0' ;;
        gcp-enable) from='1U << 31'; to='1U << 30' ;;
        repetition) from='.misc_control = { .clear_mask = 15U'; to='.misc_control = { .clear_mask = 0' ;;
        clock-stop) from='.clock_stop = 0'; to='.clock_stop = 2U' ;;
        null) from='return EINVAL;'; to='return 0;' ;;
        *) echo "Unknown HDMI0 mutant: $mutant" >&2; exit 1 ;;
    esac
    awk -v from="$from" -v to="$to" '
        { line=$0; while ((p=index(line,from)) != 0) { matches++; line=substr(line,p+length(from)) } }
        { p=index($0,from); if (p) $0=substr($0,1,p-1) to substr($0,p+length(from)); print }
        END { if (matches != 1) exit 1 }
    ' "$work/good.h" > "$work/hdmi0.h"
    cmp -s "$work/good.h" "$work/hdmi0.h" && exit 1
    compile
    if (ulimit -c 0; run) >"$work/mutant.log" 2>&1; then
        echo "HDMI0 contract accepted mutant: $mutant" >&2; exit 1
    fi
    if [ -n "${HDMI0_EVIDENCE:-}" ]; then cp "$work/mutant.log" "$HDMI0_EVIDENCE/mutant-$mutant.log"; fi
    echo "HDMI0 contract: rejected $mutant mutant"
done
