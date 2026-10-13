#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD same-device PRIME acceptance cross build.
set -eu
[ "$#" = 2 ] || { echo "usage: $0 /source /new-output" >&2; exit 2; }
: "${CC:?Select the EmberBSD target compiler}"
: "${SYSROOT:?Select the recorded EmberBSD sysroot}"
src=$1
out=$2
[ ! -e "$out" ]
mkdir -p "$out"
"$CC" --sysroot="$SYSROOT" -O2 -static -std=c11 -D_NETBSD_SOURCE \
    -Wall -Wextra -Werror -MMD -MF "$out/prime-check.d" \
    -I"$src/sys/external/bsd/drm2/dist/include/uapi/drm" \
    "$src/ember/tools/bcm2712-v3d-prime-check.c" -o "$out/prime-check"
