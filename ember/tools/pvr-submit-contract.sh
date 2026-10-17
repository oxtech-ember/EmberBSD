#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Test the complete submit implementation and real command decoder.
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/pvr-submit.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
sed '/^#include /d' "$src/sys/external/bsd/drm2/powervr/pvr_submit.c" > "$work/pvr-submit-body.h"
sed '/^#include /d' "$src/sys/external/bsd/drm2/powervr/pvr_drm.h" > "$work/pvr-uapi-body.h"
sed '/^#include /d' "$src/sys/external/bsd/drm2/powervr/pvr_exec_hwrt.h" \
    > "$work/pvr-exec-hwrt-body.h"
sed '/^#include /d' "$src/sys/external/bsd/drm2/powervr/pvr_hwrt.h" \
    > "$work/pvr-hwrt-body.h"
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
    ${CFLAGS:-} -pthread -I"$work" -I"$src/sys/arch" \
    -I"$src/sys/external/bsd/drm2/powervr" \
    "$src/ember/tools/pvr-submit-contract.c" \
    "$src/sys/external/bsd/drm2/powervr/pvr_stream.c" -o "$work/contract"
"$work/contract"
