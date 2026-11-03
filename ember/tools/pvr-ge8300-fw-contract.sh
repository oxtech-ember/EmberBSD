#!/bin/sh
# SPDX-License-Identifier: MIT
# Origin: EmberBSD GE8300 firmware CPU-only contract, AI-assisted.
set -eu
[ "$#" -eq 1 ] || { echo "usage: $0 pinned-firmware" >&2; exit 2; }
: "${PVR_HOST_PREFIX:?set selected host prefix}"
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/pvr-ge8300.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
"$PVR_HOST_PREFIX/bin/clang" -std=c11 -Wall -Wextra -Werror \
    -I"$src/sys/dev/powervr" -I"$src/sys/arch/arm/sunxi" \
    -I"$PVR_HOST_PREFIX/include" \
    "$src/sys/dev/powervr/pvr_fwinfo.c" \
    "$src/sys/dev/powervr/pvr_fwimage.c" \
    "$src/sys/dev/powervr/pvr_mips.c" \
    "$src/sys/dev/powervr/pvr_ge8300.c" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwinfo.c" \
    "$src/ember/tools/pvr-ge8300-fw-contract.c" \
    -L"$PVR_HOST_PREFIX/lib" -Wl,-rpath,"$PVR_HOST_PREFIX/lib" -lcrypto \
    -o "$work/contract"
if [ -n "${TEST_RUNNER:-}" ]; then
    $TEST_RUNNER "$work/contract" "$1"
else
    "$work/contract" "$1"
fi
