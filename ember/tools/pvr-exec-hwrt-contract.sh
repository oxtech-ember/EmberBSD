#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors. AI-assisted real-source tests.
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
[ "$#" -eq 0 ] || { echo "usage: $0" >&2; exit 2; }
work=$(mktemp -d "${TMPDIR:-/tmp}/pvr-exec-hwrt.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
${CC:-cc} -std=c11 -Wall -Wextra -Werror \
    ${PVR_EXEC_HWRT_CFLAGS:--fsanitize=address,undefined} \
    -I"$tools" "$tools/pvr-exec-hwrt-contract.c" -o "$work/contract"
"$work/contract"
