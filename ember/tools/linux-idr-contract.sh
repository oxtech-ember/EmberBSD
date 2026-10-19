#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Extract the complete native Linux idr implementation. Model kernel boundaries.
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/linux-idr.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
# Drop includes and the DDB-only symbol lookup; keep every other line.
sed -e '/^#include /d' -e '/^#ifdef _KERNEL_OPT/,/^#endif/d' \
    "$src/sys/external/bsd/drm2/linux/linux_idr.c" \
    > "$work/linux-idr-body.h"
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
    -Wno-unused-function ${CFLAGS:-} -I"$work" \
    "$src/ember/tools/linux-idr-contract.c" -o "$work/contract"
"$work/contract"
