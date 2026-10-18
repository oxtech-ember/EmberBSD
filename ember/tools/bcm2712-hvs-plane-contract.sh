#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/bcm2712-hvs-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
echo "HVS plane temporary directory: $work"
sed '/^#include /d; /^__KERNEL_RCSID(/d' \
    "$src/sys/arch/arm/broadcom/bcm2712_display_hvs.c" > "$work/hvs.h"
sed '/^#include /d' "$src/sys/arch/arm/broadcom/bcm2712_display_hvsvar.h" > "$work/hvsvar.h"
compile()
{
    # CFLAGS is a caller-supplied argument list.
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} -I"$work" \
        "$src/ember/tools/bcm2712-hvs-plane-contract.c" -o "$work/contract"
}
run()
{
    if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/contract"; else "$work/contract"; fi
}
compile
if [ -n "${HVS_OUTPUT:-}" ]; then cp "$work/contract" "$HVS_OUTPUT"; fi
if [ -n "${HVS_EVIDENCE:-}" ]; then
    mkdir -p "$HVS_EVIDENCE"
    cp "$work/"*.h "$src/ember/tools/bcm2712-hvs-plane-contract.c" "$HVS_EVIDENCE/"
fi
if [ "${HVS_COMPILE_ONLY:-0}" = 1 ]; then exit 0; fi
run
if [ "${HVS_SKIP_MUTANTS:-0}" = 1 ]; then exit 0; fi
cp "$work/hvs.h" "$work/good.h"
for mutant in ${HVS_MUTANTS:-next c0-alpha d0-alpha width dma-upper upm-base slot dma-span upm-size slot-bound output}; do
    case "$mutant" in
        next) from='9U << 24'; to='10U << 24' ;;
        c0-alpha) from='result.words[2] |= 1U << 30'; to='result.words[2] |= 0U' ;;
        d0-alpha) from='3U << 18'; to='2U << 18' ;;
        width) from='1920U - 1U'; to='1920U' ;;
        dma-upper) from='value.dma_base >> 32'; to='value.dma_base >> 28' ;;
        upm-base) from='value.upm_offset / 256U'; to='value.upm_offset / 128U' ;;
        slot) from='value.upm_slot << 10'; to='value.upm_slot << 9' ;;
        dma-span) from='value.dma_span > HVS_DMA_LIMIT - value.dma_base'; to='0' ;;
        upm-size) from='value.upm_length < HVS_UPM_BYTES'; to='0' ;;
        slot-bound) from='value.upm_slot >= 32U'; to='value.upm_slot > 32U' ;;
        output) from='if (input == NULL || out == NULL)'; to='if (out != NULL) out->words[0] = 0; if (input == NULL || out == NULL)' ;;
        *) echo "Unknown HVS mutant: $mutant" >&2; exit 1 ;;
    esac
    awk -v from="$from" -v to="$to" '
        !changed { p=index($0,from); if (p) { $0=substr($0,1,p-1) to substr($0,p+length(from)); changed++ } }
        { print } END { if (changed != 1) exit 1 }
    ' "$work/good.h" > "$work/hvs.h"
    cmp -s "$work/good.h" "$work/hvs.h" && exit 1
    compile
    if (ulimit -c 0; run) >"$work/mutant.log" 2>&1; then
        echo "HVS contract accepted mutant: $mutant" >&2; exit 1
    fi
    if [ -n "${HVS_EVIDENCE:-}" ]; then cp "$work/mutant.log" "$HVS_EVIDENCE/mutant-$mutant.log"; fi
    echo "HVS contract: rejected $mutant mutant"
done
