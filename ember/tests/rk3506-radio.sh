#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
soc=$src/sys/arch/arm/rockchip
work=$(mktemp -d "${TMPDIR:-/tmp}/rk3506-radio.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
extract() {
    awk -v name="$2" '$0 ~ "^"name"\\(" { copy=1 }
        copy { print } copy && /^}/ { found=1; exit }
        END { if (!found) exit 1 }' "$1"
}
{
    sed -n '/^#define GPIOV2_SWPORT_DR_BASE/,/^$/p' "$soc/rk_gpio.c"
    echo 'static int'
    extract "$soc/rk_gpio.c" rk_gpio_v2_pin_read
    echo 'static int'
    extract "$soc/rk_gpio.c" rk_gpio_v2_output_state
    echo 'static int'
    extract "$soc/rk_gpio.c" rk_gpio_config_output
    echo 'static int'
    extract "$soc/rk3506_radio.c" rk3506_radio_enable
} > "$work/sequence.inc"
${CC:-cc} ${CFLAGS:--O2} -std=c11 -Wall -Wextra -Werror -Wno-sign-compare -I "$work" \
    "$src/ember/tests/rk3506-radio.c" -o "$work/check"
"$work/check"
