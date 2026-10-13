#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 EmberBSD contributors. AI-assisted production-source tests.
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
if [ "$#" -gt 1 ]; then
    echo "usage: $0 [rogue_36.56.104.183_v1.fw]" >&2
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
        echo "firmware digest does not match the pinned test input" >&2
        exit 1
    fi
fi
work=$(mktemp -d "${TMPDIR:-/tmp}/a733-fwinfo.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${FWINFO_TEST_CFLAGS:-} \
    -I"$src/sys/arch/arm/sunxi" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwinfo.c" \
    "$tools/a733-gpu-fwinfo-contract.c" -o "$work/contract"
"$work/contract" "$@"
