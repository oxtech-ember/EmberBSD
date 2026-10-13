#!/bin/sh
# Execute the actual provider decoder and A733 sensor reader with fake hardware.
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/fdt-thermal.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
awk '/^struct fdtbus_thermal_controller \{/ {copy=1} copy {print}' \
    "$src/sys/dev/fdt/fdt_thermal.c" > "$work/provider.h"
awk '/^#define[[:space:]]+THS_/ {print}
/^sun60i_a733_ths_(to_mc|get_temp)\(/ {print "static int"; copy=1}
copy {print} copy && /^}/ {copy=0}' \
    "$src/sys/arch/arm/sunxi/sun60i_a733_ths.c" > "$work/sensor.h"
${CC:-cc} ${CFLAGS:-} -std=c99 -Wall -Wextra -Werror -I"$work" \
    "$src/ember/tools/fdt-thermal-contract.c" ${LDFLAGS:-} -o "$work/check"
"$work/check"
