#!/bin/sh
# Origin: EmberBSD native V3D binary synchronization contract, 2026-10-11.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/bcmv3d-sync.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
for file in bo drv job; do
    ext=c
    [ "$file" != drv ] || ext=h
    sed '/^#include /d; /^__KERNEL_RCSID(/d' \
        "$src/sys/external/bsd/drm2/bcmv3d/bcmv3d_$file.$ext" > "$work/$file-body.h"
done
"${RUBY:-ruby}" "$src/ember/tools/bcmv3d-resv-model-export.rb" "$src" "$work"
sed '/^#include /d' "$src/sys/external/bsd/drm2/dist/include/uapi/drm/v3d_drm.h" > "$work/uapi-body.h"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
    -Wno-unused-function ${CFLAGS:-} -I"$work" \
    "$src/ember/tools/bcm2712-v3d-sync-contract.c" -o "$work/check"
if [ "${V3D_COMPILE_ONLY:-0}" = 1 ]; then
    : "${V3D_OUTPUT:?set V3D_OUTPUT for a cross-built fixture}"
    cp "$work/check" "$V3D_OUTPUT"
    exit 0
fi
# shellcheck disable=SC2086
${TEST_RUNNER:-} "$work/check"
