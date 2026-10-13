#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Compile the real FreeList owner. Model only kernel DMA/backend boundaries.
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/pvr-freelist.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
sed '/^#include /d' "$src/sys/external/bsd/drm2/powervr/pvr_freelist.c" \
    > "$work/pvr-freelist-body.h"
sed '/^#include <sys\/bus.h>/d' \
    "$src/sys/external/bsd/drm2/powervr/pvr_freelist.h" > "$work/pvr-freelist-header.h"
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
    ${CFLAGS:-} -I"$work" -I"$src/sys/arch" \
    -I"$src/sys/external/bsd/drm2/powervr" \
    "$src/ember/tools/pvr-freelist-contract.c" -o "$work/contract"
"$work/contract"
