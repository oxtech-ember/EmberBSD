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
work=$(mktemp -d "${TMPDIR:-/tmp}/a733-fwprobe.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
sed '/^#include /d' "$src/sys/dev/powervr/pvr_fwprobe.c" > "$work/driver.h"
sed '/^#include /d' "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwprobe.c" >> "$work/driver.h"
sed '/^#include /d' "$src/sys/dev/powervr/pvr_fwprobe.h" > "$work/probe.h"
for kind in probe boot mem start; do
    if [ "$kind" != probe ]; then
        sed '/^#include /d' "$src/sys/dev/powervr/pvr_fw${kind}.h" > "$work/$kind.h"
    fi
    sed '/^#include /d' "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fw${kind}.h" \
        >> "$work/$kind.h"
done
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${FWPROBE_TEST_CFLAGS:-} \
    -I"$work" -I"$src/sys/arch/arm/sunxi" \
    "$tools/a733-gpu-fwprobe-contract.c" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwinit.c" "$src/sys/dev/powervr/pvr_fwinit.c" \
    "$src/sys/dev/powervr/pvr_fw_handshake.c" -o "$work/contract"
"$work/contract"
