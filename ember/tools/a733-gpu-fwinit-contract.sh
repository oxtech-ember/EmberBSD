#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 EmberBSD contributors. AI-assisted production-source tests.
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
if [ "$#" -ne 0 ]; then
    echo "usage: $0" >&2
    exit 2
fi
work=$(mktemp -d "${TMPDIR:-/tmp}/a733-fwinit.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${FWINIT_TEST_CFLAGS:-} \
    -I"$src/sys/arch/arm/sunxi" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwinit.c" \
    "$tools/a733-gpu-fwinit-contract.c" -o "$work/contract"
"$work/contract"
