#!/bin/sh
# Execute passive cooling, the processor limit broker and the CPPC limit
# from the production sources with controlled ACPI providers.
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/acpi-passive.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
tz=${1:-$src/sys/dev/acpi/acpi_tz.c}
cppc=${2:-$src/sys/dev/acpi/acpi_cppc.c}
broker=${3:-$src/sys/dev/acpi/acpi_perflimit.c}
mkdir -p "$work/dev/acpi"
cp "$src/sys/dev/acpi/acpi_perflimit.h" "$work/dev/acpi/"
awk '/^#define[[:space:]]+ATZ/ { print }
/^struct acpitz_(zone|softc) \{/ { copy = 1 }
copy { print }
copy && /^};/ { copy = 0 }' "$tz" > "$work/acpitz-types.h"
awk '/^acpitz_(passive|passive_release)\(/ { print "static void"; copy = 1 }
/^acpitz_(poll_period|sysctl_passive)\(/ { print "static int"; copy = 1 }
copy { print }
copy && /^}/ { copy = 0 }' "$tz" > "$work/acpitz-passive.h"
awk '/^typedef enum CPCPackageElement \{/ { copy = 1 }
/^struct cppc_softc \{/ { copy = 1 }
copy { print }
copy && /^(}|\};)/ { copy = 0 }' "$cppc" > "$work/cppc-types.h"
awk '/^cppc_(perf_to_freq|freq_to_perf|limited_perf)\(/ {
    print "static ACPI_INTEGER"; copy = 1
}
/^cppc_(cpufreq_sysctl|perflimit_apply)\(/ { print "static int"; copy = 1 }
copy { print }
copy && /^}/ { copy = 0 }' "$cppc" > "$work/cppc-functions.h"
sed -e '/^#include/d' "$broker" > "$work/perflimit-body.h"
${CC:-cc} ${CFLAGS:-} -std=c11 -Wall -Wextra -Werror -I"$work" \
    "$src/ember/tools/acpi-passive-contract.c" ${LDFLAGS:-} -o "$work/check"
${TEST_RUNNER:-} "$work/check"
