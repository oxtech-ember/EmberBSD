#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors. AI-assisted source regression.
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
if [ "$#" -ne 0 ]; then
    echo "usage: $0" >&2
    exit 2
fi
work=$(mktemp -d "${TMPDIR:-/tmp}/a733-fwservice.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
for kind in mem start boot probe service; do
    sed '/^#include /d' "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fw${kind}.h" > "$work/$kind.h"
done
sed '/^#include /d' "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwservice.c" > "$work/service-driver.h"
# Use the two complete native DMA functions, before the first heap helper.
sed '/^static bool/,$d; /^#include /d' "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwheap.c" > "$work/dma-driver.h"
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${FWSERVICE_TEST_CFLAGS:-} \
    -I"$work" -I"$src/sys/arch/arm/sunxi" \
    "$tools/a733-gpu-fwservice-contract.c" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_mips.c" -o "$work/contract"
"$work/contract"
