#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; native A133 PPU policy checks.
set -eu
: "${CC:?Select the native SDK compiler}"
[ "$#" -eq 0 ] || { echo "usage: $0" >&2; exit 2; }
tests=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/a133-ppu-power.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work/arm"
ln -s "$tests/.." "$work/arm/sunxi"
"$CC" -std=c11 -Wall -Wextra -Werror ${A133_PPU_TEST_CFLAGS:-} -I"$work" \
    "$tests/t_a133_ppu_power.c" "$tests/../sun50i_a133_ppu_power.c" -o "$work/test"
if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/test"; else "$work/test"; fi
