#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD production PowerVR identity contract.
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/pvr-render.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
sed '/^#include /d' "$src/sys/external/bsd/drm2/powervr/pvr_drm.h" \
    > "$work/pvr-uapi-body.h"
sed '/^#include /d' "$src/sys/external/bsd/drm2/powervr/pvr_drv.c" \
    > "$work/pvr-drv-body.h"
for name in query.h query.c; do
    sed '/^#include /d' "$src/sys/external/bsd/drm2/powervr/pvr_$name" \
        > "$work/pvr-$name-body.h"
done
sed '/^#include /d' "$src/sys/sys/rbtree.h" > "$work/rbtree.h"
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
    ${CFLAGS:-} -include "$src/ember/tools/pvr-rbtree-test.h" -I"$work" -I"$src/sys/arch/arm/sunxi" -I"$src/sys/external/bsd/drm2/powervr" "$src/ember/tools/pvr-render-contract.c" \
    "$src/common/lib/libc/gen/rb.c" -o "$work/contract"
"$work/contract"
