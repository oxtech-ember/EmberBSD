#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Compile the actual per-file VM handlers with kernel boundary models.
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/pvr-file.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
sed '/^#include /d' "$src/sys/external/bsd/drm2/powervr/pvr_drm.h" > "$work/pvr-uapi-body.h"
sed '/^#include /d' "$src/sys/external/bsd/drm2/powervr/pvr_file.c" > "$work/pvr-file-body.h"
# Retain the actual descriptor initializer and lifecycle callback fields.
sed -n '/^static const struct drm_ioctl_desc pvr_ioctls\[\] = {/,/^};/p' \
    "$src/sys/external/bsd/drm2/powervr/pvr_drv.c" > "$work/pvr-file-wire.h"
printf 'static struct test_driver test_driver = {\n' >> "$work/pvr-file-wire.h"
sed -n '/^[[:space:]]*\.open = /p; /^[[:space:]]*\.postclose = /p' \
    "$src/sys/external/bsd/drm2/powervr/pvr_drv.c" >> "$work/pvr-file-wire.h"
printf '};\n' >> "$work/pvr-file-wire.h"
for shift in 12 14; do
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
        ${CFLAGS:-} -pthread -DPAGE_SHIFT="$shift" -I"$work" \
        "$src/ember/tools/pvr-file-contract.c" -o "$work/contract"
    "$work/contract"
done
