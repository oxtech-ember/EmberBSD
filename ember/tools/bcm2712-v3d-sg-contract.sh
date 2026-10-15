#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Compile the actual BO allocator, pager and job PTE function with modeled services.
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/bcmv3d-sg.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
for file in bo drv; do
    ext=c
    [ "$file" != drv ] || ext=h
    sed '/^#include /d; /^__KERNEL_RCSID(/d' \
        "$src/sys/external/bsd/drm2/bcmv3d/bcmv3d_$file.$ext" > "$work/$file-body.h"
done
sed '/^#include /d' "$src/sys/external/bsd/drm2/dist/include/uapi/drm/v3d_drm.h" > "$work/uapi-body.h"
awk '/^bcmv3d_ptes\(/ { print "static void"; active=1 } active { print } active && /^}/ { exit }' \
    "$src/sys/external/bsd/drm2/bcmv3d/bcmv3d_job.c" > "$work/ptes-body.h"
grep -q '\.gem_uvm_ops = &bcmv3d_bo_uvm_ops,' \
    "$src/sys/external/bsd/drm2/bcmv3d/bcmv3d_drv.c"
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
    -Wno-unused-function ${CFLAGS:-} -I"$work" \
    "$src/ember/tools/bcm2712-v3d-sg-contract.c" -o "$work/contract"
"$work/contract"
[ "${V3D_SKIP_MUTANTS:-0}" != 1 ] || exit 0
cp "$work/bo-body.h" "$work/bo-good.h"
cp "$work/ptes-body.h" "$work/ptes-good.h"
ulimit -c 0
for mutant in user-contiguous internal-scattered free-one mmap-one no-address-align \
    no-address-bound no-length-sum first-pfn full-tfu-segment; do
    cp "$work/bo-good.h" "$work/bo-body.h"
    cp "$work/ptes-good.h" "$work/ptes-body.h"
    input=bo-good.h
    output=bo-body.h
    case "$mutant" in first-pfn|full-tfu-segment) input=ptes-good.h; output=ptes-body.h;; esac
    awk -v mutant="$mutant" '
        mutant == "user-contiguous" { changed += sub(/contiguous \? 1 : size \/ PAGE_SIZE/, "1") }
        mutant == "internal-scattered" { changed += sub(/sc, requested, true/, "sc, requested, false") }
        mutant == "free-one" { changed += sub(/bo->dmasegs, bo->nsegs\);/, "bo->dmasegs, 1);") }
        mutant == "mmap-one" { changed += sub(/bo->cma.dmat, bo->dmasegs, bo->nsegs,/, "bo->cma.dmat, bo->dmasegs, 1,") }
        mutant == "no-address-align" { changed += sub(/\(address \| length\)/, "length") }
        mutant == "no-address-bound" { changed += sub(/address > \(UINT64_C\(1\) << 36\) - length/, "false") }
        mutant == "no-length-sum" { changed += sub(/if \(remaining != 0\)/, "if (false)") }
        mutant == "first-pfn" { changed += sub(/dma->ds_addr >> 12/, "bo->cma.dmamap->dm_segs[0].ds_addr >> 12") }
        mutant == "full-tfu-segment" { changed += sub(/MIN\(remaining, dma->ds_len\)/, "dma->ds_len") }
        { print }
        END { if (changed != (mutant == "free-one" ? 2 : 1)) exit 1 }
    ' "$work/$input" > "$work/$output"
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
        -Wno-unused-function ${CFLAGS:-} -I"$work" \
        "$src/ember/tools/bcm2712-v3d-sg-contract.c" -o "$work/contract"
    if "$work/contract" > "$work/mutant.log" 2>&1; then
        echo "surviving SG BO mutant: $mutant" >&2
        exit 1
    fi
    echo "SG BO mutant rejected: $mutant"
done
