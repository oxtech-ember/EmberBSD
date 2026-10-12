#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=${FWSTART_SOURCE_ROOT:-$(CDPATH= cd -- "$tools/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/a733-gpu-fwstart.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
sed '/^#include /d; /^__KERNEL_RCSID(/d' \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwstart.c" > "$work/driver.h"
sed '/^#include /d' "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwstart.h" \
    > "$work/start.h"
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${FWSTART_TEST_CFLAGS:-} \
    -I"$work" -I"$src/sys/arch/arm/sunxi" \
    "$tools/a733-gpu-fwstart-contract.c" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwinfo.c" -o "$work/test"
"$work/test"
