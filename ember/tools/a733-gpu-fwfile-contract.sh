#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors. AI-assisted real-source tests.
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
[ "$#" -eq 0 ] || { echo "usage: $0" >&2; exit 2; }
work=$(mktemp -d "${TMPDIR:-/tmp}/a733-gpu-fwfile.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

sha256_file()
{
    if command -v sha256 >/dev/null 2>&1; then sha256 -q "$1"
    elif command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | awk '{print $1}'
    else shasum -a 256 "$1" | awk '{print $1}'; fi
}

${CC:-cc} -std=c11 -Wall -Wextra -Werror \
    ${FWFILE_TEST_CFLAGS:--fsanitize=address,undefined} \
    -I"$src/sys/arch/arm/sunxi" -I"$tools" \
    "$tools/a733-gpu-fwfile-contract.c" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwcompute_file.c" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwcompute.c" \
    -o "$work/fwfile"
"$work/fwfile" self

# Build the v2 packer and run it end to end on synthetic compiler artifacts.
${CC:-cc} -std=c11 -Wall -Wextra -Werror -I"$src/sys/arch/arm/sunxi" \
    -I"$tools" \
    "$tools/a733-gpu-compute-firmware.c" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwcompute_file.c" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwcompute.c" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwcompute_vm.c" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_mmu.c" \
    -o "$work/packer"
if "$work/packer" "$work" >/dev/null 2>&1; then
    echo 'packer accepted wrong argument count' >&2
    exit 1
fi
"$work/fwfile" artifacts "$work"
"$work/packer" "$work/a733-compute.bin" 0x00240038006800b7 \
    "$work/general.bin" "$work/pds.bin" "$work/usc.bin" \
    "$work/static-state.bin" 0xda00000000 > "$work/packer.out"
image=$(sed -n 's/^image_sha256=\([0-9a-f]*\) used=184$/\1/p' "$work/packer.out")
file=$(sed -n 's/^file_sha256=\([0-9a-f]*\) bytes=60292$/\1/p' "$work/packer.out")
vm=$(sed -n 's/^vm_check=windows=4 directories=4 leaves=17$/ok/p' "$work/packer.out")
[ -n "$image" ] && [ -n "$file" ] && [ "$vm" = ok ] || {
    echo 'packer output incomplete' >&2; exit 1;
}
[ "$(sha256_file "$work/a733-compute.bin")" = "$file" ] || {
    echo 'file digest mismatch' >&2; exit 1;
}
"$work/fwfile" check "$work/a733-compute.bin" 0x00240038006800b7 "$image"
echo 'PASS: packer, file digest, and cross-checked image digest agree'
