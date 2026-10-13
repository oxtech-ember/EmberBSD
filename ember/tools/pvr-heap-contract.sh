#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD production PowerVR heap contract, AI-assisted.
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/pvr-heap.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
sed '/^#include /d' "$src/sys/external/bsd/drm2/powervr/pvr_drm.h" \
    > "$work/pvr-uapi-body.h"
sed '/^#include /d' "$src/sys/external/bsd/drm2/powervr/pvr_heap.c" \
    > "$work/pvr-heap-body.h"
for shift in 12 14; do
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} \
        -DPAGE_SHIFT="$shift" -I"$work" \
        "$src/ember/tools/pvr-heap-contract.c" -o "$work/contract"
    "$work/contract"
done
