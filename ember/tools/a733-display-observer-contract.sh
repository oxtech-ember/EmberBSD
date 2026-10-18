#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; bounded read-only CCU/PCK production contract.
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=${DISPLAY_OBSERVER_SOURCE_ROOT:-$(CDPATH= cd -- "$tools/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/a733-display.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
cp "$src/sys/arch/arm/sunxi/sun60i_a733_display.h" "$work/"
sed '/^#include /d; /^__KERNEL_RCSID(/d' \
    "$src/sys/arch/arm/sunxi/sun60i_a733_display.c" > "$work/display-body.h"
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${DISPLAY_OBSERVER_CFLAGS:-} \
    -I"$work" "$tools/a733-display-observer-contract.c" -o "$work/test"
"$work/test"
