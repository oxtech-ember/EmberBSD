#!/bin/sh
# Run the genfb_pci panel PWM functions against a register model.
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/genfb-pci-pwm.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
input=${1:-$src/sys/dev/pci/genfb_pci.c}
awk '/BEGIN PANEL PWM CONTRACT/ { copy = 1; next }
/END PANEL PWM CONTRACT/ { copy = 0 }
copy { print }' "$input" > "$work/genfb-pci-pwm.h"
test -s "$work/genfb-pci-pwm.h"
${CC:-cc} ${CFLAGS:-} -std=c99 -Wall -Wextra -Werror -I"$work" \
    "$src/ember/tools/genfb-pci-pwm-contract.c" ${LDFLAGS:-} -o "$work/check"
"$work/check"
