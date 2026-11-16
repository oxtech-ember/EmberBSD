#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: original actual-source ACPI CPU capacity runner, AI-assisted.
set -eu
[ "$#" -eq 0 ] || exit 2
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/cpu-acpi-capacity.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
# Compile the production function intact: from its return type line to the
# closing brace, nothing rewritten.
awk '
    /^cpu_acpi_capacity\(/ { found++; body = 1; print prev }
    body { print }
    body && /^}$/ { body = 0 }
    { prev = $0 }
    END { if (found != 1) exit 1 }
' "$src/sys/arch/arm/acpi/cpu_acpi.c" > "$work/cpu-acpi-capacity.inc"
# CFLAGS is an intentional compiler argument list.
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} -I"$work" \
    "$src/ember/tools/cpu-acpi-capacity-contract.c" -o "$work/test"
if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/test"; else "$work/test"; fi
