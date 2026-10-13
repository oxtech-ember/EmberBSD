#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors. AI-assisted real-source regression.
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
if [ "$#" -ne 0 ]; then
    echo "usage: $0" >&2
    exit 2
fi
work=$(mktemp -d "${TMPDIR:-/tmp}/a733-fwbridge.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
for kind in mem start boot probe service bridge; do
    sed '/^#include /d' "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fw${kind}.h" > "$work/$kind.h"
done
sed '/^#include /d' "$src/sys/external/bsd/drm2/powervr/pvr_backend.h" > "$work/backend.h"
sed '/^#include /d' "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwbridge.c" > "$work/bridge-driver.h"
# Include the complete production lifecycle functions, without sysctl boilerplate.
awk '/^sun60i_gpu_run_stage\(/ { active=1; print "static void" }
    /^sun60i_gpu_exec_attach\(/ { active=1; print "static int" }
    active { print }
    active && /^}$/ { active=0 }' \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu.c" > "$work/platform-driver.h"
sed -n '/^#define A733_GPU_BASE/,/^struct sun60i_gpu_softc/p' \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu.c" | sed '$d' > "$work/platform-defines.h"
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${FWBRIDGE_TEST_CFLAGS:-} \
    -I"$work" -I"$src/sys/arch/arm/sunxi" \
    "$tools/a733-gpu-fwbridge-contract.c" -o "$work/contract"
"$work/contract"
