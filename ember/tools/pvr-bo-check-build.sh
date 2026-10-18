#!/bin/sh
# Build the native PowerVR BO check with the EmberBSD cross compiler.
set -eu
if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then
    echo "usage: CC=... SYSROOT=... $0 SOURCE NEW_OUTPUT_DIR [bo|prime]" >&2
    exit 2
fi
: "${CC:?Set CC to the EmberBSD cross compiler}"
: "${SYSROOT:?Set SYSROOT to a verified EmberBSD sysroot}"
src=$1
out=$2
case ${3:-bo} in
    bo) program=pvr-bo-check ;;
    prime) program=pvr-prime-check ;;
    *) echo "unknown probe: $3" >&2; exit 2 ;;
esac
[ ! -e "$out" ] || { echo "output already exists: $out" >&2; exit 2; }
mkdir -p "$out"
"$CC" --sysroot="$SYSROOT" -O2 -static -std=c11 -D_NETBSD_SOURCE \
    -Wall -Wextra -Werror -MMD -MF "$out/$program.d" \
    -I"$src/sys/external/bsd/drm2/dist/include/uapi" \
    -I"$src/sys/external/bsd/drm2/powervr" \
    "$src/ember/tools/$program.c" -o "$out/$program"
