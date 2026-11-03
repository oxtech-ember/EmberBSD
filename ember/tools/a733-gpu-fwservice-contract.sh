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
    case "$kind" in mem|start|boot|probe)
        sed '/^#include /d' "$src/sys/dev/powervr/pvr_fw${kind}.h" > "$work/$kind.h" ;;
    esac
    sed '/^#include /d' "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fw${kind}.h" >> "$work/$kind.h"
done
sed '/^#include /d' "$src/sys/dev/powervr/pvr_fwservice.h" > "$work/service.h"
sed '/^#include /d' "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwservice.h" >> "$work/service.h"
sed '/^#include /d' "$src/sys/dev/powervr/pvr_fwservice.c" > "$work/service-driver.h"
sed '/^#include /d' "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwservice.c" >> "$work/service-driver.h"
# Use the two complete native DMA functions, before the first heap helper.
sed '/^static bool/,$d; /^#include /d' "$src/sys/dev/powervr/pvr_fwheap.c" |
    cat > "$work/dma-driver.h"
# Simulate raw GE allocation; the checked owner has its own fwmem contract.
sed '/^void$/,$d; /^#include /d; /memset(dma->cpu, 0, size);/d'     "$src/sys/dev/powervr/pvr_fwheap.c" |     sed s/pvr_heap_dma_alloc_coherent/fake_ge_raw_alloc/g >> "$work/dma-driver.h"
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${FWSERVICE_TEST_CFLAGS:-} \
    -I"$work" -I"$src/sys/arch/arm/sunxi" \
    "$tools/a733-gpu-fwservice-contract.c" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_mips.c" "$src/sys/dev/powervr/pvr_mips.c" -o "$work/contract"
"$work/contract"
