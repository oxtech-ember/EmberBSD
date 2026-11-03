#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; native A133 GPU PLL callback checks.
set -eu
: "${CC:?Select the native SDK compiler}"
[ "$#" -eq 0 ] || { echo "usage: $0" >&2; exit 2; }
tests=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/a133-gpu-pll.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
"$CC" -std=c11 -Wall -Wextra -Werror -Wno-unused-function \
    -Wno-unused-parameter ${A133_GPU_PLL_TEST_CFLAGS:-} \
    "$tests/t_a133_gpu_pll.c" -o "$work/test"
if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/test"; else "$work/test"; fi
