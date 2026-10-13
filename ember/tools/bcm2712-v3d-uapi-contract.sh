#!/bin/sh
# Origin: EmberBSD V3D 7.1 UAPI contract, 2026-10-11.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/bcmv3d-uapi.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
sed '/^#include /d' \
    "$src/sys/external/bsd/drm2/dist/include/uapi/drm/v3d_drm.h" \
    > "$work/v3d-uapi-body.h"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -I"$work" \
    "$src/ember/tools/bcm2712-v3d-uapi-contract.c" -o "$work/contract"
"$work/contract"
