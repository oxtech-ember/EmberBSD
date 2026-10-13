#!/bin/sh
# Test the production FDT fan parser and thermal control with fake providers.
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/pwm-fan.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
input=${1:-$src/sys/dev/fdt/pwm_fan.c}
awk '/^struct pwm_fan_(trip|softc) \{/ { copy = 1 }
copy { print }
copy && /^};/ { copy = 0 }' "$input" > "$work/pwm-fan-types.h"
awk '
/^pwm_fan_(read_u32|levels|add_trip|parse_map|parse|set|demand|sysctl|detach)\(/ {
    print "static int"; copy = 1
}
/^pwm_fan_(suspend|resume|shutdown)\(/ { print "static bool"; copy = 1 }
/^pwm_fan_update\(/ { print "static void"; copy = 1 }
copy { print }
copy && /^}/ { copy = 0 }' "$input" > "$work/pwm-fan-control.h"
${CC:-cc} ${CFLAGS:-} -std=c99 -Wall -Wextra -Werror -I"$work" \
    "$src/ember/tools/pwm-fan-contract.c" ${LDFLAGS:-} -o "$work/check"
"$work/check"
