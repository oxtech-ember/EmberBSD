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
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwinit.h" "$work/"
# Stage 18 needs the serializer ABI and the packed compute-image format;
# stages 19-21 add the firmware-heap submission provider API.
for header in fwcompute fwcompute_cmd; do
    printf '#include "powervr/pvr_rogue_fwif_client.h"\n' > "$work/sun60i_a733_gpu_$header.h"
    sed '/^#include /d' "$src/sys/dev/powervr/pvr_$header.h" >> "$work/sun60i_a733_gpu_$header.h"
    sed '/^#include /d' "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_$header.h" >> "$work/sun60i_a733_gpu_$header.h"
done
printf %s\\n "#include \"sun60i_a733_gpu_fwcompute_cmd.h\"" > "$work/sun60i_a733_gpu_fwcompute_file.h"
sed "/^#include /d" "$src/sys/dev/powervr/pvr_fwcompute_file.h" >> "$work/sun60i_a733_gpu_fwcompute_file.h"
sed "/^#include /d" "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwcompute_file.h" >> "$work/sun60i_a733_gpu_fwcompute_file.h"
sed "/^#include /d" "$src/sys/dev/powervr/pvr_fwheap.h" > "$work/sun60i_a733_gpu_fwheap.h"
sed "/^#include /d" "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwheap.h" >> "$work/sun60i_a733_gpu_fwheap.h"
for name in drm.h query.h backend.h; do
    sed '/^#include /d' "$src/sys/external/bsd/drm2/powervr/pvr_$name" \
        > "$work/pvr-$name-body.h"
done
mkdir "$work/powervr"
cp "$src/sys/dev/powervr/fwif/"*.h "$work/powervr/"
# Keep the production memory interface. Only native bus-DMA types are stubbed.
for header in fwmem fwstart fwboot fwprobe fwservice fwbridge; do
    case "$header" in
    fwmem|fwstart|fwboot|fwprobe|fwservice)
        sed '/^#include /d' "$src/sys/dev/powervr/pvr_$header.h" > "$work/sun60i_a733_gpu_$header.h" ;;
    fwbridge)
        sed '/^#include /d' "$src/sys/external/bsd/drm2/powervr/pvr_fwbridge.h" > "$work/sun60i_a733_gpu_$header.h" ;;
    esac
    sed '/^#include /d' "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_$header.h" \
        >> "$work/sun60i_a733_gpu_$header.h"
done
sed '/^#include /d' "$src/sys/external/bsd/drm2/powervr/pvr_backend.h" \
    > "$work/pvr-backend.h-body.h"
# The platform driver calls the common firmware-info adapter. Compile its
# production implementation with only the native declarations needed here.
{
    printf '%s\n' '#include <errno.h>' '#include <stdbool.h>' '#include <stdint.h>' \
        '#include <stddef.h>' '#include "pvr-query.h-body.h"' \
        '#include <dev/powervr/pvr_fwinfo.h>' \
        '#ifndef __arraycount' '#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))' '#endif'
    awk '/^pvr_query_input_from_fwinfo\(/ { print "int"; capture = 1 } \
        capture { print; if ($0 == "}") exit }' \
        "$src/sys/external/bsd/drm2/powervr/pvr_query.c"
} > "$work/pvr-query-input.c"
# Exercise the real parent-specific queue and finalization dispatch as well.
autoconf="$src/sys/kern/subr_autoconf.c"
sed -n '1,/^#include /p' "$autoconf" | sed '/^#include /d' \
    > "$work/autoconf.h"
sed -n '/^struct deferred_config {/,/^};/p; /^struct finalize_hook {/,/^};/p' \
    "$autoconf" > "$work/autoconf-structs.h"
for name in config_defer config_mountroot_done config_process_deferred config_finalize_register \
    config_finalize config_mountroot config_mountroot_thread; do
    case "$name" in
    config_mountroot_done|config_process_deferred|config_mountroot_thread) printf 'static void\n' ;;
    config_finalize_register) printf 'int\n' ;;
    *) printf 'void\n' ;;
    esac >> "$work/autoconf.h"
    sed -n "/^$name(/,/^}/p" "$autoconf" >> "$work/autoconf.h"
done
${CC:-cc} -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
    ${GPU_ID_TEST_CFLAGS:-} -I"$work" -I"$src/sys/arch/arm/sunxi" -idirafter "$src/sys" \
    "$tools/a733-gpu-identification-contract.c" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwinfo.c" "$src/sys/dev/powervr/pvr_fwinfo.c" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwcompute_cmd.c" "$src/sys/dev/powervr/pvr_fwcompute_cmd.c" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwcompute_file.c" \
    "$src/sys/dev/powervr/pvr_fwcompute_file.c" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwcompute.c" "$src/sys/dev/powervr/pvr_fwcompute.c" \
    "$work/pvr-query-input.c" -o "$work/test"
"$work/test"
