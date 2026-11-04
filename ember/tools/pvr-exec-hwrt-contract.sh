#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors. AI-assisted real-source tests.
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
[ "$#" -eq 0 ] || { echo "usage: $0" >&2; exit 2; }
work=$(mktemp -d "${TMPDIR:-/tmp}/pvr-exec-hwrt.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
printf 'int\n' > "$work/hwrt-admission.inc"
sed -n '/^pvr_create_hwrt_dataset(/,/^}/p' \
    "$src/sys/external/bsd/drm2/powervr/pvr_exec_hwrt.c" >> "$work/hwrt-admission.inc"
printf 'static void\n' > "$work/hwrt-production.inc"
sed -n '/^hwrt_tiles(/,/^}/p' "$src/sys/external/bsd/drm2/powervr/pvr_exec_hwrt.c" >> "$work/hwrt-production.inc"
printf 'static void\n' >> "$work/hwrt-production.inc"
sed -n '/^hwrt_tiles_v1(/,/^}/p' "$src/sys/external/bsd/drm2/powervr/pvr_exec_hwrt.c" >> "$work/hwrt-production.inc"
printf 'static uint32_t\n' >> "$work/hwrt-production.inc"
sed -n '/^hwrt_next_pow2(/,/^}/p' "$src/sys/external/bsd/drm2/powervr/pvr_exec_hwrt.c" >> "$work/hwrt-production.inc"
printf 'static int\n' >> "$work/hwrt-production.inc"
sed -n '/^hwrt_spans(/,/^}/p' "$src/sys/external/bsd/drm2/powervr/pvr_exec_hwrt.c" >> "$work/hwrt-production.inc"
${CC:-cc} -std=c11 -Wall -Wextra -Werror \
    ${PVR_EXEC_HWRT_CFLAGS:--fsanitize=address,undefined} \
    -I"$work" -I"$tools" "$tools/pvr-exec-hwrt-contract.c" -o "$work/contract"
"$work/contract"
