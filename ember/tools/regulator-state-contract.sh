#!/bin/sh
# Origin: EmberBSD; production regulator state query regression checks.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=${REGULATOR_SOURCE_ROOT:-$(CDPATH= cd -- "$tools/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/regulator-state.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
fdt="$src/sys/dev/fdt/fdt_regulator.c"
axp="$src/sys/dev/i2c/axp8191.c"
# Retain upstream notices while compiling the actual functions and tables.
sed -n '1,/^#include /p' "$fdt" | sed '/^#include /d' > "$work/fdt.h"
sed -n '/^struct fdtbus_regulator_controller_func {/,/^};/p' \
    "$src/sys/dev/fdt/fdtvar.h" >> "$work/fdt.h"
for name in enable disable is_enabled; do
    printf 'int\n' >> "$work/fdt-functions.h"
    sed -n "/^fdtbus_regulator_$name(/,/^}/p" "$fdt" \
        >> "$work/fdt-functions.h"
done
sed -n '1,/^#include /p' "$axp" | sed '/^#include /d' > "$work/axp.h"
sed -n '/^#define.*AXP8191_DCDC_CTL1/,/^struct axp8191reg_attach_args {/p' \
    "$axp" | sed '$d' >> "$work/axp.h"
for name in axp8191_read axp8191_write axp8191reg_acquire \
    axp8191reg_enable axp8191reg_is_enabled axp8191reg_get_voltage \
    axp8191reg_set_voltage; do
    printf 'static int\n' >> "$work/axp-functions.h"
    sed -n "/^$name(/,/^}/p" "$axp" >> "$work/axp-functions.h"
done
printf 'static void\n' >> "$work/axp-functions.h"
sed -n '/^axp8191reg_release(/,/^}/p' "$axp" >> "$work/axp-functions.h"
sed -n '/^static const struct fdtbus_regulator_controller_func axp8191reg_funcs = {/,/^};/p' \
    "$axp" >> "$work/axp-functions.h"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
    ${REGULATOR_TEST_CFLAGS:-} -I"$work" \
    "$tools/regulator-state-contract.c" -o "$work/test"
"$work/test"
