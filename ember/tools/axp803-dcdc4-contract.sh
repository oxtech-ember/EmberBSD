#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; test actual PMIC code without hardware.
set -eu
: "${CC:?Select the native SDK compiler}"
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/axp803-dcdc4.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
file="$src/sys/dev/i2c/axppmic.c"
for name in axppmic_axp803_dcdc4_950 axppmic_set_voltage axpreg_set_voltage; do
    sed -n "/^$name(/,/^}/p" "$file" | if grep -q .; then
        printf 'static int\n'; sed -n "/^$name(/,/^}/p" "$file"
    fi >> "$work/axp803-actual.inc"
done
"$CC" -std=c11 -Wall -Wextra -Werror -Wno-unused-function -Wno-sign-compare -I"$work" \
    "$src/ember/tools/axp803-dcdc4-contract.c" -o "$work/test"
"$work/test"
