#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD real A133 compute workflow, modelled bus and firmware.
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
[ "$#" -eq 1 ] || { echo "usage: $0 GE_COMPUTE_IMAGE" >&2; exit 2; }
work=$(mktemp -d "${TMPDIR:-/tmp}/a133-compute.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
printf 'static int\n' > "$work/consumer.inc"
sed -n '/^a133_gpu_compute_run(/,/^}/p' "$src/sys/arch/arm/sunxi/sun50i_a133_gpu.c" >> "$work/consumer.inc"
for name in a133_gpu_drm_health a133_gpu_drm_attach; do
    if grep -q "^$name(" "$src/sys/arch/arm/sunxi/sun50i_a133_gpu.c"; then
        printf 'static int\n' >> "$work/consumer.inc"
        sed -n "/^$name(/,/^}/p" "$src/sys/arch/arm/sunxi/sun50i_a133_gpu.c" >> "$work/consumer.inc"
    fi
done
crypto_include= crypto_lib= crypto_rpath=
if [ -n "${PVR_HOST_PREFIX:-}" ]; then
    crypto_include="-I$PVR_HOST_PREFIX/include"
    crypto_lib="-L$PVR_HOST_PREFIX/lib"
    crypto_rpath="-Wl,-rpath,$PVR_HOST_PREFIX/lib"
fi
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-deprecated-declarations \
    -fsanitize=address,undefined -I"$work" ${crypto_include:+"$crypto_include"} \
    "$src/ember/tools/a133-gpu-compute-consumer-contract.c" \
    "$src/sys/dev/powervr/pvr_fwcompute.c" \
    "$src/sys/dev/powervr/pvr_fwcompute_file.c" \
    "$src/sys/dev/powervr/pvr_fwcompute_cmd.c" \
    ${crypto_lib:+"$crypto_lib"} ${crypto_rpath:+"$crypto_rpath"} -lcrypto -o "$work/test"
"$work/test" "$1"
