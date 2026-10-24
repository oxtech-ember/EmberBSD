#!/bin/sh
# Execute thermal control and zone refresh from the production driver.
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/acpitz-fan.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
input=${1:-$src/sys/dev/acpi/acpi_tz.c}
awk '/^#define[[:space:]]+ATZ/ { print }
/^struct acpitz_(zone|softc) \{/ { copy = 1 }
copy { print }
copy && /^};/ { copy = 0 }' "$input" > "$work/acpitz-types.h"
awk '
/^acpitz_(get_zone_quiet|get_status|set_active|power_off|get_zone|sane_temp|passive)\(/ {
    print "static void"; copy = 1
}
/^acpitz_(same_cooling|valid_level)\(/ { print "static bool"; copy = 1 }
/^acpitz_sysctl_fan(_min)?\(/ { print "static int"; copy = 1 }
copy { print }
copy && /^}/ { copy = 0 }' "$input" > "$work/acpitz-control.h"
${CC:-cc} ${CFLAGS:-} -std=c99 -Wall -Wextra -Werror -I"$work" \
    "$src/ember/tools/acpitz-fan-contract.c" ${LDFLAGS:-} -o "$work/check"
${TEST_RUNNER:-} "$work/check"
