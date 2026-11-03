#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu
crypto_include= crypto_lib= crypto_rpath=
if [ -n "${PVR_HOST_PREFIX:-}" ]; then
    crypto_include="-I$PVR_HOST_PREFIX/include"
    crypto_lib="-L$PVR_HOST_PREFIX/lib"
    crypto_rpath="-Wl,-rpath,$PVR_HOST_PREFIX/lib"
fi
if [ "$#" -gt 2 ]; then
    echo "usage: $0 [a733-fw [ge8300-fw]]" >&2
    exit 2
fi
if [ "$#" -ge 1 ]; then
    if command -v sha256 >/dev/null 2>&1; then
        actual=$(sha256 -q "$1")
    elif command -v sha256sum >/dev/null 2>&1; then
        actual=$(sha256sum "$1" | awk '{print $1}')
    else
        actual=$(shasum -a 256 "$1" | awk '{print $1}')
    fi
    expected=1db1c399c17401d1f79d46c880db81c724d748c784d4639433b076aba2f9c0d2
    if [ "$actual" != "$expected" ]; then
        echo 'The firmware SHA256 does not match the pinned image.' >&2
        exit 1
    fi
fi
if [ "$#" -eq 2 ]; then
    if command -v sha256 >/dev/null 2>&1; then
        actual=$(sha256 -q "$2")
    elif command -v sha256sum >/dev/null 2>&1; then
        actual=$(sha256sum "$2" | awk '{print $1}')
    else
        actual=$(shasum -a 256 "$2" | awk '{print $1}')
    fi
    [ "$actual" = b571cdd90312c20fe87f14aae43484f7279859921ae3abe58e935412040c7f98 ] || {
        echo 'The GE firmware SHA256 does not match the pinned image.' >&2; exit 1; }
fi
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=${FWSTART_SOURCE_ROOT:-$(CDPATH= cd -- "$tools/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/a733-gpu-fwstart.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
sed '/^#include /d; /^__KERNEL_RCSID(/d' \
    "$src/sys/dev/powervr/pvr_fwstart.c" > "$work/driver.h"
sed '/^#include /d' "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwstart.c" >> "$work/driver.h"
sed '/^#include /d' "$src/sys/dev/powervr/pvr_fwstart.h" > "$work/start.h"
sed '/^#include /d' "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwstart.h" \
    >> "$work/start.h"
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${FWSTART_TEST_CFLAGS:-} \
    -I"$work" -I"$src/sys/arch/arm/sunxi" -I"$src/sys/dev/powervr" ${crypto_include:+"$crypto_include"} \
    "$tools/a733-gpu-fwstart-contract.c" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwinfo.c" "$src/sys/dev/powervr/pvr_fwinfo.c" \
    "$src/sys/dev/powervr/pvr_fwimage.c" "$src/sys/dev/powervr/pvr_mips.c" \
    "$src/sys/dev/powervr/pvr_ge8300.c" ${crypto_lib:+"$crypto_lib"} \
    ${crypto_rpath:+"$crypto_rpath"} -lcrypto -o "$work/test"
"$work/test" "$@"
