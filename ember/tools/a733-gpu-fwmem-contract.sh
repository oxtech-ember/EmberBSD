#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Exercise the native memory owner with real firmware parsers and encoders.
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
if [ "$#" -eq 1 ]; then
    if command -v sha256 >/dev/null 2>&1; then
        digest=$(sha256 -q "$1")
    elif command -v sha256sum >/dev/null 2>&1; then
        digest=$(sha256sum "$1" | awk '{print $1}')
    else
        digest=$(shasum -a 256 "$1" | awk '{print $1}')
    fi
    if [ "$digest" != 1db1c399c17401d1f79d46c880db81c724d748c784d4639433b076aba2f9c0d2 ]; then
        echo 'firmware digest does not match the pinned test input' >&2
        exit 1
    fi
elif [ "$#" -ne 0 ]; then
    echo 'usage: a733-gpu-fwmem-contract.sh [pinned-firmware]' >&2; exit 1
fi
work=$(mktemp -d "${TMPDIR:-/tmp}/a733-gpu-fwmem.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
sed '/^#include /d; /^__KERNEL_RCSID(/d' \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwmem.c" > "$work/driver.h"
sed '/^#include /d' "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwmem.h" \
    > "$work/memory.h"
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${FWMEM_TEST_CFLAGS:-} \
    -I"$work" -I"$src/sys/arch/arm/sunxi" \
    "$tools/a733-gpu-fwmem-contract.c" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwinfo.c" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwinit.c" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwimage.c" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_mips.c" -o "$work/test"
"$work/test" "$@"
