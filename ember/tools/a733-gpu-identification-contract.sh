#!/bin/sh
# Origin: EmberBSD; execute the firmware-ready GPU identification driver.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=${GPU_ID_SOURCE_ROOT:-$(CDPATH= cd -- "$tools/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/a733-gpu-id.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
sed '/^#include /d; /^__KERNEL_RCSID(/d' \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu.c" > "$work/driver.h"
cp "$src/sys/arch/arm/sunxi/sun60i_a733_ccu.h" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_pck600.h" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwinfo.h" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwimage.h" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwinit.h" "$work/"
# Stage 18 needs the serializer ABI and the packed compute-image format;
# stages 19-21 add the firmware-heap submission provider API.
cp "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwcompute.h" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwcompute_cmd.h" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwcompute_file.h" "$work/"
sed '/^#include /d' "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwheap.h"     > "$work/sun60i_a733_gpu_fwheap.h"
mkdir "$work/powervr"
cp "$src/sys/arch/arm/sunxi/powervr/"*.h "$work/powervr/"
# Keep the production memory interface. Only native bus-DMA types are stubbed.
for header in fwmem fwstart fwboot fwprobe; do
    sed '/^#include /d' "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_$header.h" \
        > "$work/sun60i_a733_gpu_$header.h"
done
# Exercise the real parent-specific queue and finalization dispatch as well.
autoconf="$src/sys/kern/subr_autoconf.c"
sed -n '1,/^#include /p' "$autoconf" | sed '/^#include /d' \
    > "$work/autoconf.h"
sed -n '/^struct deferred_config {/,/^};/p; /^struct finalize_hook {/,/^};/p' \
    "$autoconf" > "$work/autoconf-structs.h"
for name in config_defer config_process_deferred config_finalize_register \
    config_finalize; do
    case "$name" in
    config_process_deferred) printf 'static void\n' ;;
    config_finalize_register) printf 'int\n' ;;
    *) printf 'void\n' ;;
    esac >> "$work/autoconf.h"
    sed -n "/^$name(/,/^}/p" "$autoconf" >> "$work/autoconf.h"
done
${CC:-cc} -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
    ${GPU_ID_TEST_CFLAGS:-} -I"$work" \
    "$tools/a733-gpu-identification-contract.c" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwinfo.c" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwcompute_cmd.c" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwcompute_file.c" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwcompute.c" -o "$work/test"
"$work/test"
