#!/bin/sh
# Origin: EmberBSD; exercise the production A100/A133 GPU clock callbacks.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
if [ -z "${CC:-}" ]; then
	printf '%s\n' 'CC must name the selected SDK host compiler' >&2
	exit 2
fi
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=${GPU_CLOCK_SOURCE_ROOT:-$(CDPATH= cd -- "$tools/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/a100-gpu-clock.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
sed -n '/^sunxi_ccu_update_bits(/,/^}/p' "$src/sys/arch/arm/sunxi/sunxi_ccu.c" |
    { printf 'static int\n'; cat; } > "$work/ccu-rmw.h"
sed -n '/^sunxi_ccu_reset_assert_unguarded(/,/^}/p' \
    "$src/sys/arch/arm/sunxi/sunxi_ccu.c" |
    { printf 'static int\n'; cat; } >> "$work/ccu-rmw.h"
sed -n '/^sunxi_ccu_reset_deassert_unguarded(/,/^}/p' \
    "$src/sys/arch/arm/sunxi/sunxi_ccu.c" |
    { printf 'static int\n'; cat; } >> "$work/ccu-rmw.h"
sed '/^#include /d; /^__KERNEL_RCSID(/d' \
	"$src/sys/arch/arm/sunxi/sunxi_ccu_div.c" > "$work/ccu-div.h"
sed '/^#include /d; /^__KERNEL_RCSID(/d' \
    "$src/sys/arch/arm/sunxi/sunxi_ccu_gate.c" > "$work/ccu-gate.h"
"$CC" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
    ${GPU_CLOCK_TEST_CFLAGS:-} -I"$src/sys/arch" \
    -I"$src/sys/external/gpl2/dts/dist/include" \
    -I"$work" \
    "$tools/a100-gpu-clock-contract.c" -o "$work/test"
if [ "${GPU_CLOCK_STATIC_ANALYZE:-0}" = 1 ]; then
	if [ -z "${ANALYZER_CC:-}" ]; then
		printf '%s\n' 'ANALYZER_CC must name the selected SDK Clang analyzer' >&2
		exit 2
	fi
	"$ANALYZER_CC" --analyze -std=c11 -Wall -Wextra -Wno-unused-parameter \
        -I"$src/sys/arch" -I"$src/sys/external/gpl2/dts/dist/include" \
        -I"$work" "$tools/a100-gpu-clock-contract.c"
fi
if [ "${GPU_CLOCK_CPPCHECK:-0}" = 1 ]; then
	if [ -z "${CPPCHECK:-}" ]; then
		printf '%s\n' 'CPPCHECK must name the selected SDK Cppcheck' >&2
		exit 2
	fi
	"$CPPCHECK" --enable=warning,performance,portability \
        --error-exitcode=1 --std=c11 -I"$src/sys/arch" \
        -I"$src/sys/external/gpl2/dts/dist/include" -I"$work" \
        "$tools/a100-gpu-clock-contract.c"
fi
"$work/test"
