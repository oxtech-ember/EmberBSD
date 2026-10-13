#!/bin/sh
# Origin: EmberBSD native render BO lifetime contract, 2026-10-11.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/bcmv3d-bo.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
sed '/^#include /d; /^__KERNEL_RCSID(/d' \
    "$src/sys/external/bsd/drm2/bcmv3d/bcmv3d_bo.c" > "$work/bo-body.h"
sed '/^#include /d' \
    "$src/sys/external/bsd/drm2/bcmv3d/bcmv3d_drv.h" > "$work/drv-body.h"
sed '/^#include /d' \
    "$src/sys/external/bsd/drm2/dist/include/uapi/drm/v3d_drm.h" \
    > "$work/uapi-body.h"
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
    ${CFLAGS:-} -I"$work" "$src/ember/tools/bcm2712-v3d-bo-contract.c" \
    -o "$work/contract"
"$work/contract"
