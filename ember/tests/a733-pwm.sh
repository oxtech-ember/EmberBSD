#!/bin/sh
# Run the production PWM functions against fault-injecting MMIO.
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/a733-pwm.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
input=${1:-$src/sys/arch/arm/sunxi/sun60i_a733_pwm.c}
awk '/BEGIN PWM CONTRACT/ { copy = 1; next }
/END PWM CONTRACT/ { copy = 0 }
copy { print }' "$input" > "$work/a733-pwm-control.h"
cp "$src/sys/dev/pwm/pwmvar.h" "$work/pwmvar.h"
${CC:-cc} ${CFLAGS:-} -std=c99 -Wall -Wextra -Werror -I"$work" \
    "$src/ember/tools/a733-pwm-contract.c" ${LDFLAGS:-} -o "$work/check"
"$work/check"
