#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 EmberBSD contributors. AI-assisted native owner checks.
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
if [ "$#" -gt 1 ]; then
    echo "usage: $0 [pinned-firmware]" >&2
    exit 2
fi
if [ "$#" -eq 1 ]; then
    if command -v sha256 >/dev/null 2>&1; then
        digest=$(sha256 -q "$1")
    elif command -v sha256sum >/dev/null 2>&1; then
        digest=$(sha256sum "$1" | awk '{print $1}')
    else
        digest=$(shasum -a 256 "$1" | awk '{print $1}')
    fi
    if [ "$digest" != 1db1c399c17401d1f79d46c880db81c724d748c784d4639433b076aba2f9c0d2 ]; then
        echo 'firmware digest does not match the pinned input' >&2
        exit 1
    fi
fi
work=$(mktemp -d "${TMPDIR:-/tmp}/a733-gpu-vm.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
sed '/^#include /d' "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_vm.c" > "$work/driver.h"
sed '/^#include /d' "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_vm.h" > "$work/vm.h"
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${VM_TEST_CFLAGS:-} \
    -I"$work" -I"$src/sys/arch/arm/sunxi" \
    "$tools/a733-gpu-vm-contract.c" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_mmu.c" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwinfo.c" -o "$work/test"
"$work/test" "$@"
