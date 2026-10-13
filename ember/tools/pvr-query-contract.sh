#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD production PowerVR metadata-query contract.
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
firmware=${2:-${PVR_QUERY_FIRMWARE:-}}
if [ -z "$firmware" ]; then
    echo 'Set PVR_QUERY_FIRMWARE or pass source-root firmware-path.' >&2
    exit 2
fi
expected=1db1c399c17401d1f79d46c880db81c724d748c784d4639433b076aba2f9c0d2
actual=$(openssl dgst -sha256 "$firmware" | awk '{print $NF}')
[ "$actual" = "$expected" ] || { echo 'Unexpected firmware SHA256.' >&2; exit 1; }
work=$(mktemp -d "${TMPDIR:-/tmp}/pvr-query.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
for name in drm.h query.h query.c; do
    sed '/^#include /d' "$src/sys/external/bsd/drm2/powervr/pvr_$name" \
        > "$work/pvr-$name-body.h"
done
sed -n '/^\/\* Convert parsed firmware metadata/,/^\/\* Reserve registration/p' \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu.c" | sed '$d' \
    > "$work/adapter-body.h"
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} \
    -I"$work" -I"$src/sys/arch/arm/sunxi" \
    "$src/ember/tools/pvr-query-contract.c" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwinfo.c" -o "$work/contract"
"$work/contract" "$firmware"
