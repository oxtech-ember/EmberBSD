#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
[ "$#" = 2 ] || { echo "usage: $0 /source /new-output" >&2; exit 2; }
: "${CC:?Select the EmberBSD target compiler}"
: "${SYSROOT:?Select the recorded EmberBSD sysroot}"
src=$1
out=$2
[ ! -e "$out" ]
mkdir -p "$out"
"$CC" --sysroot="$SYSROOT" -O2 -static -std=c11 -D_NETBSD_SOURCE \
    -Wall -Wextra -Werror -pthread -MMD -MF "$out/drm-syncobj-check.d" \
    -I"$src/sys/external/bsd/drm2/dist/include/uapi/drm" \
    "$src/ember/tools/drm-syncobj-check.c" -o "$out/drm-syncobj-check"
