#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Extract the complete native chain implementation. Model kernel boundaries.
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/dma-fence-chain.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
sed '/^#include /d' \
    "$src/sys/external/bsd/drm2/linux/linux_dma_fence_chain.c" \
    > "$work/dma-fence-chain-body.h"
# Keep the common status operation real; only its kernel boundaries are models.
{ echo int; sed -n '/^dma_fence_get_status(/,/^}/p' \
    "$src/sys/external/bsd/drm2/linux/linux_dma_fence.c"; } \
    > "$work/dma-fence-status-body.h"
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
    ${CFLAGS:-} -I"$work" "$src/ember/tools/dma-fence-chain-contract.c" \
    -o "$work/contract"
"$work/contract"
