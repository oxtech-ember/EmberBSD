#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; native checks for the experimental vendor PPU adapter.
set -eu
: "${CC:?Select the native SDK compiler}"
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
tmp=$(mktemp -d "${TMPDIR:-/tmp}/a133-gpu-fdt.XXXXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
"$CC" -std=c99 -Wall -Wextra -Werror ${CPPFLAGS:-} -UNDEBUG \
    "$src/ember/tools/a133-gpu-fdt-test.c" \
    "$src/sys/arch/arm/sunxi/sun50i_a133_fdt.c" \
    ${LDFLAGS:-} -lfdt -o "$tmp/test"
"$tmp/test"
