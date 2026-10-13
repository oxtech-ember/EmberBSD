#!/bin/sh
# Origin: EmberBSD; AI-assisted production FDT pre-attach power regressions.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=${FDT_ATTACH_SOURCE_ROOT:-$(CDPATH= cd -- "$tools/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/fdt-power-attach.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
bus="$src/sys/dev/fdt/fdtbus.c"
sed -n '/^struct fdt_node {/,/^};/p; /^struct fdt_softc {/,/^};/p' \
    "$bus" > "$work/nodes.h"
sed -n '/^struct fdtbus_powerdomain_controller_func {/,/^};/p' \
    "$src/sys/dev/fdt/fdtvar.h" > "$work/interface.h"
sed '/^#include /d; /^__KERNEL_RCSID(/d' \
    "$src/sys/dev/fdt/fdt_powerdomain.c" > "$work/power.h"
# Preserve the original notice and compile the actual match/scan/attach bodies.
sed -n '1,/^#include /p' "$bus" | sed '/^#include /d' > "$work/attach.h"
printf 'static void\n' >> "$work/attach.h"
sed -n '/^fdt_scan_best(/,/^fdt_add_node(/p' "$bus" |
    sed '$d' | sed '$d' >> "$work/attach.h"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
    ${FDT_ATTACH_TEST_CFLAGS:-} -I"$work" \
    -I"$src/sys/external/gpl2/dts/dist/include" \
    "$tools/fdt-power-attach-contract.c" -o "$work/test"
"$work/test"
