#!/bin/sh
# Origin: EmberBSD actual-source CSD ownership/cache contract, 2026-10-11.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/bcmv3d-csd.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
for file in bo drv job; do
    ext=c
    [ "$file" != drv ] || ext=h
    sed '/^#include /d; /^__KERNEL_RCSID(/d' \
        "$src/sys/external/bsd/drm2/bcmv3d/bcmv3d_$file.$ext" > "$work/$file-body.h"
done
sed '/^#include /d' "$src/sys/external/bsd/drm2/dist/include/uapi/drm/v3d_drm.h" > "$work/uapi-body.h"
compile()
{
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
        -Wno-unused-function ${CFLAGS:-} -I"$work" \
        "$src/ember/tools/bcm2712-v3d-csd-contract.c" -o "$work/contract"
}
compile
"$work/contract"
[ "${V3D_SKIP_MUTANTS:-0}" != 1 ] || exit 0
cp "$work/job-body.h" "$work/job-good.h"
ulimit -c 0
for mutant in csd-count csd-busy cache-clean cfg7 address pre-sync post-sync core-mask; do
    awk -v mutant="$mutant" '
        mutant == "csd-count" { changed += sub(/atomic_load_acquire\(&job->core_events\) != 1/, "false") }
        mutant == "csd-busy" { changed += sub(/\(after & 15\) != 0/, "false") }
        mutant == "cache-clean" { changed += sub(/error = bcmv3d_cache_wait\(5, 1\);/, "error = 0;") }
        mutant == "cfg7" { changed += sub(/i <= 7;/, "i <= 6;") }
        mutant == "address" { changed += sub(/!bcmv3d_csd_address\(job, a->cfg\[5\] & ~7U, 8\)/, "false") }
        mutant == "pre-sync" { changed += sub(/BUS_DMASYNC_PREWRITE \| BUS_DMASYNC_PREREAD\);/, "BUS_DMASYNC_PREWRITE);") }
        mutant == "post-sync" { changed += sub(/BUS_DMASYNC_POSTWRITE \| BUS_DMASYNC_POSTREAD\);/, "BUS_DMASYNC_POSTWRITE);") }
        mutant == "core-mask" { changed += sub(/if \(!job->core_masked\)/, "if (true)") }
        { print }
        END { if (changed != (mutant == "pre-sync" ? 2 : 1)) exit 1 }
    ' "$work/job-good.h" > "$work/job-body.h"
    compile
    if "$work/contract" > "$work/mutant.log" 2>&1; then
        echo "surviving CSD mutant: $mutant" >&2
        exit 1
    fi
    echo "CSD mutant rejected: $mutant"
done
