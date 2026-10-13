#!/bin/sh
# Origin: EmberBSD; AI-assisted tests of the production A733 clock provider.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=${CLOCK_SOURCE_ROOT:-$(CDPATH= cd -- "$tools/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/a733-clocks.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
for name in sunxi_rtcvar.h sunxi_ccu.h sun60i_a733_ccu.h sun60i_a733_ccu.c \
    sunxi_ccu_gate.c sunxi_ccu_div.c sunxi_ccu_nm.c \
    sunxi_ccu_nkmp.c sunxi_ccu_fixed_factor.c; do
    sed '/^#include /d; /^__KERNEL_RCSID(/d' \
        "$src/sys/arch/arm/sunxi/$name" > "$work/$name"
done
for kind in clock reset; do
    cat "$src/sys/external/gpl2/dts/dist/include/dt-bindings/$kind/allwinner,sun60i-a733-ccu.h"
done > "$work/bindings.h"
awk '$1 == "#define" && $2 ~ /^A733_/ {
    name = substr($2, 6)
    printf "_Static_assert(%s == %s, \"%s binding drift\");\n", $2, name, name
}' "$src/sys/arch/arm/sunxi/sun60i_a733_ccu.h" >> "$work/bindings.h"
# Use the production dispatch and reset operations, with their original notices.
sed -n '1,/^#include /p' "$src/sys/arch/arm/sunxi/sunxi_ccu.c" |
    sed '/^#include /d' > "$work/dispatch.h"
sed -n '/^sunxi_ccu_clock_get_rate(/,/^static const struct clk_funcs/p' \
    "$src/sys/arch/arm/sunxi/sunxi_ccu.c" | sed '$d' |
    { printf 'static u_int\n'; cat; } >> "$work/dispatch.h"
sed -n '/^sunxi_ccu_reset_assert_unguarded(/,/^static const struct fdtbus_reset/p' \
    "$src/sys/arch/arm/sunxi/sunxi_ccu.c" | sed '$d' |
    { printf 'static int\n'; cat; } >> "$work/dispatch.h"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
    ${CLOCK_TEST_CFLAGS:-} -I"$work" "$tools/a733-accelerator-clock-fixture.c" \
    -o "$work/test"
"$work/test"
