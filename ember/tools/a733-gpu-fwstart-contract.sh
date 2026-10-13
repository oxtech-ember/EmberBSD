#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu
if [ "$#" -gt 1 ]; then
    echo "usage: $0 [rogue_36.56.104.183_v1.fw]" >&2
    exit 2
fi
if [ "$#" -eq 1 ]; then
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
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=${FWSTART_SOURCE_ROOT:-$(CDPATH= cd -- "$tools/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/a733-gpu-fwstart.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
sed '/^#include /d; /^__KERNEL_RCSID(/d' \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwstart.c" > "$work/driver.h"
sed '/^#include /d' "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwstart.h" \
    > "$work/start.h"
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${FWSTART_TEST_CFLAGS:-} \
    -I"$work" -I"$src/sys/arch/arm/sunxi" \
    "$tools/a733-gpu-fwstart-contract.c" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwinfo.c" -o "$work/test"
"$work/test" "$@"
