#!/bin/sh
# Origin: EmberBSD V3D CL acceptance cross build, 2026-10-11.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
[ "$#" = 2 ] || { echo "usage: $0 /source /new-output-directory" >&2; exit 2; }
src=$1
out=$2
: "${CC:?Select the EmberBSD target C compiler}"
: "${SYSROOT:?Select the recorded EmberBSD sysroot}"
[ ! -e "$out" ] || { echo "output directory already exists" >&2; exit 1; }
mkdir -p "$out"
# Extract the unchanged, accepted packet builder. No second packet encoding.
awk '
    /^#define QV3D_/ { print }
    /^struct qv3d_cl \{/ { body = 1 }
    /^\/\* --- bus_dma objects/ { body = 0 }
    body { print }
' "$src/sys/arch/arm/broadcom/bcm2712_v3d_queue.c" > "$out/cl-packets.h"
"$CC" --sysroot="$SYSROOT" -O2 -static -pthread -std=c11 -Wall -Wextra \
    -Werror -Wno-unused-function -MMD -MF "$out/cl-check.d" \
    -I"$src/sys/external/bsd/drm2/dist/include/uapi/drm" -I"$out" \
    "$src/ember/tools/bcm2712-v3d-cl-check.c" -o "$out/cl-check"
