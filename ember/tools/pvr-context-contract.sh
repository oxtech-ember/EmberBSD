#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Run the native context source with a firmware boundary model.
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/pvr-context.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
sed '/^#include /d' "$src/sys/external/bsd/drm2/powervr/pvr_context.c" > "$work/pvr-context-body.h"
sed '/^#include /d' "$src/sys/external/bsd/drm2/powervr/pvr_drm.h" > "$work/pvr-drm-body.h"
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} \
    -I"$work" -I"$src/sys/arch" -I"$src/sys/external/bsd/drm2/powervr" \
    "$src/ember/tools/pvr-context-contract.c" \
    "$src/sys/external/bsd/drm2/powervr/pvr_stream.c" -o "$work/contract"
"$work/contract"
