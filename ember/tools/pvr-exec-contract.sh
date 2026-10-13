#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors. AI-assisted real-source regression.
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
if [ "$#" -ne 0 ]; then echo "usage: $0" >&2; exit 2; fi
work=$(mktemp -d "${TMPDIR:-/tmp}/pvr-exec.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
for part in backend context drm exec file freelist stream; do
    sed '/^#include /d' "$src/sys/external/bsd/drm2/powervr/pvr_$part.h" > "$work/$part.h"
done
sed '/^#include /d' "$src/sys/external/bsd/drm2/powervr/pvr_exec.c" > "$work/exec-driver.h"
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${PVR_EXEC_TEST_CFLAGS:-} \
    -I"$work" "$tools/pvr-exec-contract.c" -o "$work/contract"
"$work/contract"
