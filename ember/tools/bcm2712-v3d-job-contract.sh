#!/bin/sh
# Origin: EmberBSD actual-source TFU ownership contract, 2026-10-11.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/bcmv3d-job.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
for file in bo drv job; do
    ext=c
    [ "$file" != drv ] || ext=h
    sed '/^#include /d; /^__KERNEL_RCSID(/d' \
        "$src/sys/external/bsd/drm2/bcmv3d/bcmv3d_$file.$ext" \
        > "$work/$file-body.h"
done
sed '/^#include /d' \
    "$src/sys/external/bsd/drm2/dist/include/uapi/drm/v3d_drm.h" \
    > "$work/uapi-body.h"
compile()
{
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
        -Wno-unused-function ${CFLAGS:-} -I"$work" \
        "$src/ember/tools/bcm2712-v3d-job-contract.c" -o "$work/contract"
}
compile
"$work/contract"
[ "${V3D_SKIP_MUTANTS:-0}" != 1 ] || exit 0
cp "$work/job-body.h" "$work/job-good.h"
ulimit -c 0
for mutant in flags source-write completion-count busy unmap revoke \
    prewrite preread postread early-return-to-cpu; do
    awk -v mutant="$mutant" '
        mutant == "flags" { changed += sub(/a->flags != 0/, "false") }
        mutant == "source-write" { changed += sub(/i == 1 \?/, "true ?") }
        mutant == "completion-count" {
            changed += sub(/atomic_load_acquire\(&job->events\) != 1/, "false")
        }
        mutant == "busy" { changed += sub(/\(after & 1\) != 0/, "false") }
        mutant == "unmap" { changed += sub(/bcmv3d_ptes\(job, false\)/, "bcmv3d_ptes(job, true)") }
        mutant == "revoke" { changed += sub(/error = bcmv3d_flush\(\);/, "(void)bcmv3d_flush(); error = 0;") }
        mutant == "prewrite" { changed += sub(/bcmv3d_sync\(job->src, BUS_DMASYNC_PREWRITE\);/, "/* missing source PREWRITE */") }
        mutant == "preread" { changed += sub(/bcmv3d_sync\(job->dst, BUS_DMASYNC_PREREAD\);/, "/* missing destination PREREAD */") }
        mutant == "postread" { changed += sub(/bcmv3d_sync\(job->dst, BUS_DMASYNC_POSTREAD\);/, "/* missing destination POSTREAD */") }
        mutant == "early-return-to-cpu" {
            changed += sub(/bcmv3d_ptes\(job, false\);/, "bcmv3d_sync(job->dst, BUS_DMASYNC_POSTREAD); bcmv3d_ptes(job, false);")
        }
        { print }
        END { if (changed != 1) exit 1 }
    ' "$work/job-good.h" > "$work/job-body.h"
    compile
    if "$work/contract" > "$work/mutant.log" 2>&1; then
        echo "surviving TFU ownership mutant: $mutant" >&2
        exit 1
    fi
    echo "TFU ownership mutant rejected: $mutant"
done
