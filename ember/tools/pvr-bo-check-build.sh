#!/bin/sh
# Build the native PowerVR BO check with the EmberBSD cross compiler.
set -eu
if [ "$#" -ne 2 ]; then
    echo "usage: CC=... SYSROOT=... $0 SOURCE NEW_OUTPUT_DIR" >&2
    exit 2
fi
: "${CC:?Set CC to the EmberBSD cross compiler}"
: "${SYSROOT:?Set SYSROOT to a verified EmberBSD sysroot}"
src=$1
out=$2
[ ! -e "$out" ] || { echo "output already exists: $out" >&2; exit 2; }
mkdir -p "$out"
"$CC" --sysroot="$SYSROOT" -O2 -static -std=c11 -D_NETBSD_SOURCE \
    -Wall -Wextra -Werror -MMD -MF "$out/pvr-bo-check.d" \
    -I"$src/sys/external/bsd/drm2/dist/include/uapi" \
    -I"$src/sys/external/bsd/drm2/powervr" \
    "$src/ember/tools/pvr-bo-check.c" -o "$out/pvr-bo-check"
