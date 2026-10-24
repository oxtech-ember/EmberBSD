#!/bin/sh
# Execute the production GTDT platform timer walk with byte-built tables.
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/acpi-gtdt.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
input=${1:-$src/sys/dev/acpi/acpi.c}
awk '/^acpi_gtdt_walk\(/ { print "static void"; copy = 1 }
copy { print }
copy && /^}/ { copy = 0 }' "$input" > "$work/gtdt-walk.h"
${CC:-cc} ${CFLAGS:-} -std=c11 -Wall -Wextra -Werror \
    -DACPI_MISALIGNMENT_NOT_SUPPORTED \
    -isystem "$src/sys/external/bsd/acpica/dist/include" -I"$work" \
    "$src/ember/tools/acpi-gtdt-contract.c" ${LDFLAGS:-} -o "$work/check"
${TEST_RUNNER:-} "$work/check"
