#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Test syncobj transfer flattening against the actual native fence chain.
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/drm-sync-flatten.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
sed '/^#include /d' "$src/sys/external/bsd/drm2/linux/linux_dma_fence_chain.c" \
    > "$work/dma-fence-chain-body.h"
awk '/^#define DRM_SYNCOBJ_FLATTEN_VISITS/ { take=1 }
    take && /^static int drm_syncobj_transfer_to_timeline/ { exit }
    take { print }' "$src/sys/external/bsd/drm2/dist/drm/drm_syncobj.c" > "$work/flatten-body.h"
grep -q 'drm_syncobj_flatten(struct dma_fence' "$work/flatten-body.h"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
    ${CFLAGS:-} -I"$work" "$src/ember/tools/drm-syncobj-flatten-contract.c" \
    -o "$work/contract"
"$work/contract"
