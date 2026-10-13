#!/bin/sh
# Origin: EmberBSD native V3D DMA probe contract, 2026-10-10.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/bcm2712-v3d-dma.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
sed '/^#include /d; /^__KERNEL_RCSID(/d' \
    "$src/sys/arch/arm/broadcom/bcm2712_v3d_dma.c" > "$work/dma-body.h"
compile_contract()
{
    # Compile the actual production probe body; replace only host services.
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
        ${CFLAGS:-} -I"$work" \
        "$src/ember/tools/bcm2712-v3d-dma-contract.c" -o "$1"
}
run_contract()
{
    if [ -n "${TEST_RUNNER:-}" ]; then
        "$TEST_RUNNER" "$1"
    else
        "$1"
    fi
}
compile_contract "$work/contract"
if [ -n "${V3D_DMA_OUTPUT:-}" ]; then
    cp "$work/contract" "$V3D_DMA_OUTPUT"
fi
if [ "${V3D_DMA_COMPILE_ONLY:-0}" = 1 ]; then
    exit 0
fi
run_contract "$work/contract"
if [ "${V3D_DMA_SKIP_MUTANTS:-0}" = 1 ]; then
    exit 0
fi
cp "$work/dma-body.h" "$work/dma-good.h"
for mutation in postwrite-only source-writeable four-pages skip-pt-prewrite \
    release-always icfg-early mmuc-bit31 counter-any; do
    cp "$work/dma-good.h" "$work/dma-body.h"
    awk -v mutation="$mutation" '
        /BUS_DMASYNC_POSTREAD \| BUS_DMASYNC_POSTWRITE/ && mutation == "postwrite-only" {
            # The actual-destination post sync is the third combined sync.
            if (++seen_post == 3) {
                sub(/BUS_DMASYNC_POSTREAD \| /, "")
                changed = 1
            }
        }
        mutation == "source-writeable" {
            changed += sub(/source_pfn \| DV3D_PTE_VALID;/,
                "source_pfn | DV3D_PTE_VALID | DV3D_PTE_WRITEABLE;")
        }
        mutation == "four-pages" {
            changed += sub(/for \(i = 0; i < DV3D_IMAGE_PAGES; i\+\+\)/,
                "for (i = 0; i < 4; i++)")
        }
        mutation == "skip-pt-prewrite" {
            changed += sub(/DV3D_OBJ_PT\]\.size, BUS_DMASYNC_PREWRITE\)/,
                "DV3D_OBJ_PT].size, 0)")
        }
        mutation == "release-always" {
            changed += sub(/if \(!dv3d\.published\)/, "if (1)")
        }
        mutation == "icfg-early" {
            changed += sub(/poke\(DV3D_TFU_IIS, DV3D_TFU_DIM\)/,
                "poke(DV3D_TFU_ICFG, DV3D_TFU_ICFG_VALUE)")
        }
        mutation == "mmuc-bit31" {
            changed += sub(/DV3D_MMUC_ENABLE \| DV3D_MMUC_FLUSH\)/,
                "UINT32_C(0x80000003))")
        }
        mutation == "counter-any" {
            changed += sub(/dv3d_cvtct\(value\) == expected/,
                "dv3d_cvtct(value) != expected")
        }
        { print }
        END { exit changed > 0 ? 0 : 1 }
    ' "$work/dma-good.h" > "$work/dma-body.h" || {
        echo "mutant $mutation: source pattern not found" >&2
        exit 1
    }
    if ! compile_contract "$work/contract-mutant"; then
        echo "mutant $mutation failed to compile" >&2
        exit 1
    fi
    if run_contract "$work/contract-mutant" >/dev/null 2>&1; then
        echo "mutant $mutation was not rejected" >&2
        exit 1
    fi
done
echo "PASS: DMA probe contract; 8 causal mutants rejected"
