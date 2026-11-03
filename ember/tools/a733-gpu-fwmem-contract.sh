#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Exercise the native memory owner with real firmware parsers and encoders.
set -eu
crypto_include= crypto_lib= crypto_rpath=
if [ -n "${PVR_HOST_PREFIX:-}" ]; then
    crypto_include="-I$PVR_HOST_PREFIX/include"
    crypto_lib="-L$PVR_HOST_PREFIX/lib"
    crypto_rpath="-Wl,-rpath,$PVR_HOST_PREFIX/lib"
fi
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
if [ "$#" -ge 1 ] && [ "$#" -le 2 ]; then
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
    if [ "$#" -eq 2 ]; then
        if command -v sha256 >/dev/null 2>&1; then
            digest=$(sha256 -q "$2")
        elif command -v sha256sum >/dev/null 2>&1; then
            digest=$(sha256sum "$2" | awk '{print $1}')
        else
            digest=$(shasum -a 256 "$2" | awk '{print $1}')
        fi
        if [ "$digest" != b571cdd90312c20fe87f14aae43484f7279859921ae3abe58e935412040c7f98 ]; then
            echo 'GE8300 firmware does not match the pinned test input' >&2
            exit 1
        fi
    fi
elif [ "$#" -ne 0 ]; then
    echo 'usage: a733-gpu-fwmem-contract.sh [a733-fw [ge8300-fw]]' >&2; exit 1
fi
work=$(mktemp -d "${TMPDIR:-/tmp}/a733-gpu-fwmem.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
sed '/^#include /d; /^__KERNEL_RCSID(/d' \
    "$src/sys/dev/powervr/pvr_fwmem.c" > "$work/driver.h"
sed '/^#include /d' "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwmem.c" >> "$work/driver.h"
sed '/^#include /d' "$src/sys/dev/powervr/pvr_fwmem.h" > "$work/memory.h"
sed '/^#include /d' "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwmem.h" >> "$work/memory.h"
sed "/^#include /d" "$src/sys/dev/powervr/pvr_fwheap.h" > "$work/heap.h"
sed "/^#include /d" "$src/sys/dev/powervr/pvr_fwheap.c" > "$work/heap-driver.h"
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${FWMEM_TEST_CFLAGS:-} \
    -I"$work" -I"$src/sys/arch/arm/sunxi" -I"$src/sys/dev/powervr" ${crypto_include:+"$crypto_include"} \
    "$tools/a733-gpu-fwmem-contract.c" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwinfo.c" "$src/sys/dev/powervr/pvr_fwinfo.c" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwinit.c" "$src/sys/dev/powervr/pvr_fwinit.c" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwimage.c" "$src/sys/dev/powervr/pvr_fwimage.c" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_mips.c" "$src/sys/dev/powervr/pvr_mips.c" \
    "$src/sys/dev/powervr/pvr_ge8300.c" \
    "$src/sys/dev/powervr/pvr_fwcompute_vm.c" "$src/sys/dev/powervr/pvr_mmu.c" \
    "$src/sys/dev/powervr/pvr_fwcompute_file.c" "$src/sys/dev/powervr/pvr_fwcompute_cmd.c" ${crypto_lib:+"$crypto_lib"} \
    ${crypto_rpath:+"$crypto_rpath"} -lcrypto -o "$work/test"
if [ -n "${TEST_RUNNER:-}" ]; then
    $TEST_RUNNER "$work/test" "$@"
else
    "$work/test" "$@"
fi
