#!/bin/sh
# Origin: EmberBSD; AI-assisted A733 power-domain production regressions.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=${POWER_SOURCE_ROOT:-$(CDPATH= cd -- "$tools/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/a733-power.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
sed -n '/^struct fdtbus_powerdomain_controller_func {/,/^};/p' \
    "$src/sys/dev/fdt/fdtvar.h" > "$work/power-interface.h"
sed '/^#include /d; /^__KERNEL_RCSID(/d' \
    "$src/sys/dev/fdt/fdt_powerdomain.c" > "$work/power-fdt.h"
sed '/^#include /d; /^__KERNEL_RCSID(/d' \
    "$src/sys/arch/arm/sunxi/sun60i_a733_pck600.c" > "$work/power-driver.h"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
    ${POWER_TEST_CFLAGS:-} -I"$work" "$tools/a733-power-fixture.c" \
    -o "$work/test"
"$work/test"
