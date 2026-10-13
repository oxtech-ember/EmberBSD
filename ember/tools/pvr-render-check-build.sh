#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD native PowerVR identity acceptance cross build.
set -eu
[ "$#" = 2 ] || { echo "usage: $0 /source /new-output" >&2; exit 2; }
: "${CC:?Select the EmberBSD target compiler}"
: "${SYSROOT:?Select the recorded EmberBSD sysroot}"
src=$1
out=$2
[ ! -e "$out" ]
mkdir -p "$out"
"$CC" --sysroot="$SYSROOT" -O2 -static -std=c11 -D_NETBSD_SOURCE \
    -Wall -Wextra -Werror -MMD -MF "$out/pvr-render-check.d" \
    -I"$src/sys/external/bsd/drm2/dist/include/uapi" \
    -I"$src/sys/external/bsd/drm2/powervr" \
    "$src/ember/tools/pvr-render-check.c" -o "$out/pvr-render-check"
