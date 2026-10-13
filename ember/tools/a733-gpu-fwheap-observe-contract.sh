#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors. AI-assisted real-source tests.
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
[ "$#" -eq 0 ] || { echo "usage: $0" >&2; exit 2; }
work=$(mktemp -d "${TMPDIR:-/tmp}/a733-fwheap-observe.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
sed '/^#include /d' "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwheap.h" > "$work/heap.h"
printf 'void\n' > "$work/observer.h"
sed -n '/^sun60i_a733_gpu_fwheap_observe(/,/^}/p' \
    "${FWHEAP_OBSERVE_SOURCE:-$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwheap.c}" \
    >> "$work/observer.h"
${CC:-cc} -std=c11 -Wall -Wextra -Werror \
    ${FWHEAP_OBSERVE_CFLAGS:--fsanitize=address,undefined} \
    -I"$work" -I"$src/sys/arch/arm/sunxi" \
    "$tools/a733-gpu-fwheap-observe-contract.c" -o "$work/contract"
"$work/contract"
