#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/bcm2712-phy-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
echo "PHY core temporary directory: $work"
sed '/^#include /d; /^__KERNEL_RCSID(/d' \
    "$src/sys/arch/arm/broadcom/bcm2712_display_phy.c" > "$work/phy.h"
sed '/^#include /d' "$src/sys/arch/arm/broadcom/bcm2712_display_phyvar.h" > "$work/phyvar.h"
compile()
{
    # CFLAGS is a caller-supplied argument list.
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} -I"$work" \
        "$src/ember/tools/bcm2712-display-phy-contract.c" -o "$work/contract"
}
run()
{
    if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/contract"; else "$work/contract"; fi
}
compile
if [ -n "${PHY_OUTPUT:-}" ]; then cp "$work/contract" "$PHY_OUTPUT"; fi
if [ -n "${PHY_EVIDENCE:-}" ]; then
    mkdir -p "$PHY_EVIDENCE"
    cp "$work/"*.h "$src/ember/tools/bcm2712-display-phy-contract.c" "$PHY_EVIDENCE/"
fi
if [ "${PHY_COMPILE_ONLY:-0}" = 1 ]; then exit 0; fi
run
if [ "${PHY_SKIP_MUTANTS:-0}" = 1 ]; then exit 0; fi
cp "$work/phy.h" "$work/good.h"
for mutant in ${PHY_MUTANTS:-rm-value window bounds rmw mutation fault fault-barrier reinit oneshot disable-count disable-flag cached-status}; do
    case "$mutant" in
        rm-value) from='0x98100000'; to='0x98000000' ;;
        window) from='BCM2712_RM_WINDOW, 0x018'; to='BCM2712_PHY_WINDOW, 0x018' ;;
        bounds) from='0x194, 0x1'; to='0x300, 0x1' ;;
        rmw) from='value & ~1U'; to='0' ;;
        mutation) from='sc->status.mutation_attempted = true;'; to='/* missing mutation flag */' ;;
        fault) from='sc->status.fault = error;'; to='sc->status.fault = 0;' ;;
        fault-barrier) from='sc->status.fault = error;'; to='sc->io.barrier(sc->io.arg); sc->status.fault = error;' ;;
        reinit) from='if (out->initialized)'; to='if (false)' ;;
        oneshot) from='if (sc->status.stage != BCM2712_PHY_IDLE)'; to='if (false)' ;;
        disable-count) from='i < 3;'; to='i < 2;' ;;
        disable-flag) from='sc->status.programming_complete = false;'; to='/* stale completion flag */' ;;
        cached-status) from='*out = sc->status;'; to='sc->io.barrier(sc->io.arg); *out = sc->status;' ;;
        *) echo "Unknown PHY mutant: $mutant" >&2; exit 1 ;;
    esac
    awk -v from="$from" -v to="$to" '
        !changed { p=index($0,from); if (p) { $0=substr($0,1,p-1) to substr($0,p+length(from)); changed++ } }
        { print } END { if (changed != 1) exit 1 }
    ' "$work/good.h" > "$work/phy.h"
    compile
    if (ulimit -c 0; run) >"$work/mutant.log" 2>&1; then
        echo "PHY contract accepted mutant: $mutant" >&2; exit 1
    fi
    if [ -n "${PHY_EVIDENCE:-}" ]; then cp "$work/mutant.log" "$PHY_EVIDENCE/mutant-$mutant.log"; fi
    echo "PHY contract: rejected $mutant mutant"
done
