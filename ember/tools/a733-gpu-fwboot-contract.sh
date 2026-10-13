#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 EmberBSD contributors. AI-assisted source tests.
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
if [ "$#" -ne 0 ]; then
    echo "usage: $0" >&2
    exit 2
fi
work=$(mktemp -d "${TMPDIR:-/tmp}/a733-fwboot.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
sed '/^#include /d' "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwboot.c" \
    > "$work/driver.h"
sed '/^#include /d' "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwboot.h" \
    > "$work/boot.h"
sed '/^#include /d' "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwmem.h" \
    > "$work/memory.h"
sed '/^#include /d' "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwstart.h" \
    > "$work/start.h"
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${FWBOOT_TEST_CFLAGS:-} \
    -I"$work" -I"$src/sys/arch/arm/sunxi" \
    "$tools/a733-gpu-fwboot-contract.c" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwinit.c" -o "$work/contract"
"$work/contract"
