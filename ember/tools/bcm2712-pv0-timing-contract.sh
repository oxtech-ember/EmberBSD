#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/bcm2712-pv0-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
echo "PV0 timing temporary directory: $work"
sed '/^#include /d; /^__KERNEL_RCSID(/d' \
    "$src/sys/arch/arm/broadcom/bcm2712_display_pv0.c" > "$work/pv0.h"
sed '/^#include /d' "$src/sys/arch/arm/broadcom/bcm2712_display_pv0var.h" > "$work/pv0var.h"
compile()
{
    # CFLAGS is a caller-supplied argument list.
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} -I"$work" \
        "$src/ember/tools/bcm2712-pv0-timing-contract.c" -o "$work/contract"
}
run()
{
    if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/contract"; else "$work/contract"; fi
}
compile
if [ -n "${PV0_OUTPUT:-}" ]; then cp "$work/contract" "$PV0_OUTPUT"; fi
if [ -n "${PV0_EVIDENCE:-}" ]; then
    mkdir -p "$PV0_EVIDENCE"
    cp "$work/"*.h "$src/ember/tools/bcm2712-pv0-timing-contract.c" "$PV0_EVIDENCE/"
fi
if [ "${PV0_COMPILE_ONLY:-0}" = 1 ]; then exit 0; fi
run
if [ "${PV0_SKIP_MUTANTS:-0}" = 1 ]; then exit 0; fi
cp "$work/pv0.h" "$work/good.h"
for mutant in ${PV0_MUTANTS:-selector ppc odd-timing pv-enable video-enable fifo packing}; do
    case "$mutant" in
        selector|ppc) : ;;
        odd-timing) from='(1U << 29)'; to='0U' ;;
        pv-enable) from='(PV0_CLOCK_SELECT << 2) | (1U << 1)'; to='(PV0_CLOCK_SELECT << 2) | 3U' ;;
        video-enable) from='.v_control = (1U << 29) | (1U << 1)'; to='.v_control = (1U << 29) | 3U' ;;
        fifo) from='64U - 3U * 6U'; to='64U - 3U * 5U' ;;
        packing) from='(88U / PV0_PPC) << 16'; to='(88U / PV0_PPC) << 15' ;;
        *) echo "Unknown PV0 mutant: $mutant" >&2; exit 1 ;;
    esac
    case "$mutant" in
        selector) sed '/^#define PV0_CLOCK_SELECT/s/0U/1U/' "$work/good.h" > "$work/pv0.h" ;;
        ppc) sed '/^#define PV0_PPC/s/1U/2U/' "$work/good.h" > "$work/pv0.h" ;;
        *) awk -v from="$from" -v to="$to" '
            !changed { p=index($0,from); if (p) { $0=substr($0,1,p-1) to substr($0,p+length(from)); changed++ } }
            { print } END { if (changed != 1) exit 1 }
        ' "$work/good.h" > "$work/pv0.h" ;;
    esac
    cmp -s "$work/good.h" "$work/pv0.h" && exit 1
    compile
    if (ulimit -c 0; run) >"$work/mutant.log" 2>&1; then
        echo "PV0 contract accepted mutant: $mutant" >&2; exit 1
    fi
    if [ -n "${PV0_EVIDENCE:-}" ]; then cp "$work/mutant.log" "$PV0_EVIDENCE/mutant-$mutant.log"; fi
    echo "PV0 contract: rejected $mutant mutant"
done
