#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD. Actual-source V3D completion sleep contract.
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/bcmv3d-wait.XXXXXXXX")
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
        "$src/ember/tools/bcm2712-v3d-wait-contract.c" -o "$work/contract"
}
compile
if [ "${V3D_COMPILE_ONLY:-0}" = 1 ]; then
    : "${V3D_OUTPUT:?set V3D_OUTPUT for a cross-built fixture}"
    cp "$work/contract" "$V3D_OUTPUT"
    exit 0
fi
if [ -n "${V3D_WAIT_EVIDENCE:-}" ]; then
    mkdir -p "$V3D_WAIT_EVIDENCE"
    cp "$work/bo-body.h" "$work/drv-body.h" "$work/job-body.h" \
        "$work/uapi-body.h" "$V3D_WAIT_EVIDENCE/"
fi
# shellcheck disable=SC2086
${TEST_RUNNER:-} "$work/contract"
[ "${V3D_SKIP_MUTANTS:-0}" != 1 ] || exit 0
cp "$work/job-body.h" "$work/job-good.h"
ulimit -c 0
for mutant in tsleep unlock-before-enqueue; do
    ruby - "$work/job-good.h" "$work/job-body.h" "$mutant" <<'RUBY'
source, output, mutant = ARGV
text = File.read(source)
call = /\(void\)mtsleep\(job, PWAIT, wmesg, ticks, &job->irq_lock\);/
replacement = if mutant == 'tsleep'
  'mutex_exit(&job->irq_lock); (void)tsleep(job, PWAIT, wmesg, ticks); mutex_enter(&job->irq_lock);'
else
  'mutex_exit(&job->irq_lock); mutex_enter(&job->irq_lock); (void)mtsleep(job, PWAIT, wmesg, ticks, &job->irq_lock);'
end
abort 'missing wait call' unless text.sub!(call, replacement)
File.write(output, text)
RUBY
    compile
    for engine in 0 1 2; do
        if V3D_WAIT_ENGINE="$engine" "$work/contract" > "$work/mutant.log" 2>&1; then
            echo "surviving V3D wait mutant: $mutant, engine $engine" >&2
            exit 1
        fi
        echo "V3D wait mutant rejected: $mutant, engine $engine"
    done
done
