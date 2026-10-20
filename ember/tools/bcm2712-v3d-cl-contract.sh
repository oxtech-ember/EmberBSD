#!/bin/sh
# Origin: EmberBSD native CL ownership contract, 2026-10-11.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/bcmv3d-cl.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
for file in bo drv job; do
    ext=c
    [ "$file" != drv ] || ext=h
    sed '/^#include /d; /^__KERNEL_RCSID(/d' \
        "$src/sys/external/bsd/drm2/bcmv3d/bcmv3d_$file.$ext" > "$work/$file-body.h"
done
"${RUBY:-ruby}" "$src/ember/tools/bcmv3d-resv-model-export.rb" "$src" "$work"
sed '/^#include /d' "$src/sys/external/bsd/drm2/dist/include/uapi/drm/v3d_drm.h" > "$work/uapi-body.h"
compile()
{
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
    -Wno-unused-function ${CFLAGS:-} -I"$work" \
    "$src/ember/tools/bcm2712-v3d-cl-contract.c" -o "$work/contract"
}
compile
if [ "${V3D_COMPILE_ONLY:-0}" = 1 ]; then
    : "${V3D_OUTPUT:?set V3D_OUTPUT for a cross-built fixture}"
    cp "$work/contract" "$V3D_OUTPUT"
    exit 0
fi
# shellcheck disable=SC2086
${TEST_RUNNER:-} "$work/contract"
[ "${V3D_SKIP_MUTANTS:-0}" != 1 ] || exit 0
cp "$work/job-body.h" "$work/job-good.h"
ulimit -c 0
for mutant in irq-identity completion-count stale-irq retire-irq cache-clean \
    pre-sync post-sync address input-rcl diagnostic-pcs diagnostic-read-error diagnostic-invalid-value; do
    awk -v mutant="$mutant" '
        mutant == "irq-identity" {
            changed += sub(/status != job->core_expected/, "false")
            changed += sub(/V3D_INT_CLR, job->core_expected/, "V3D_INT_CLR, status")
        }
        mutant == "diagnostic-pcs" { changed += sub(/V3D_CLE_PCS, true/, "0x08, true") }
        mutant == "diagnostic-read-error" { changed += sub(/failed \|= 1U << i;/, "failed |= 0;") }
        mutant == "diagnostic-invalid-value" { changed += sub(/value\[i\] = 0;/, "/* invalid read value retained */") }
        /^bcmv3d_cl_phase\(/ { cl = 1 }
        /^bcmv3d_wait_bo\(/ { cl = 0 }
        cl && mutant == "completion-count" {
            changed += sub(/atomic_load_acquire\(&job->core_events\) != 1/, "false")
        }
        cl && mutant == "stale-irq" && /EBUSY/ { next }
        cl && mutant == "stale-irq" && /error != 0 \|\| status != 0/ && !changed {
            changed++; next
        }
        cl && mutant == "retire-irq" && /return error != 0 \? error : EIO;/ { next }
        cl && mutant == "retire-irq" && /error != 0 \|\| status != 0/ {
            boundary++
            if (boundary == 2) { changed++; next }
        }
        cl && mutant == "cache-clean" { changed += sub(/error = bcmv3d_cache_wait\(5, 1\);/, "error = 0;") }
        cl && mutant == "pre-sync" { changed += sub(/BUS_DMASYNC_PREWRITE \| BUS_DMASYNC_PREREAD\);/, "BUS_DMASYNC_PREWRITE);") }
        cl && mutant == "post-sync" { changed += sub(/BUS_DMASYNC_POSTWRITE \| BUS_DMASYNC_POSTREAD\);/, "BUS_DMASYNC_POSTWRITE);") }
        cl && mutant == "address" { changed += sub(/!bcmv3d_csd_address\(job, a->rcl_end - 1, 1\)/, "false") }
        cl && mutant == "input-rcl" { changed += sub(/a->in_sync_rcl, 0,/, "0, 0,") }
        { print }
        END { if (changed != (mutant == "pre-sync" || mutant == "irq-identity" ? 2 : 1)) exit 1 }
    ' "$work/job-good.h" > "$work/job-body.h"
    compile
    if ${TEST_RUNNER:-} "$work/contract" > "$work/mutant.log" 2>&1; then
        echo "surviving CL mutant: $mutant" >&2
        exit 1
    fi
    echo "CL mutant rejected: $mutant"
done
