#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Actual-source PowerResource ownership contract. AI-assisted.
set -eu
[ "$#" -eq 0 ] || exit 2
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/acpi-power-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
case "${COMPILE_ONLY:-0}:${ACPI_POWER_ANALYZE:-0}" in
    0:0|0:1|1:0|1:1) ;;
    *) echo 'COMPILE_ONLY and ACPI_POWER_ANALYZE must be 0 or 1' >&2; exit 2 ;;
esac
if [ "${COMPILE_ONLY:-0}" = 1 ] && [ -z "${ACPI_POWER_TEST_OUTPUT:-}" ]; then
    echo 'COMPILE_ONLY requires ACPI_POWER_TEST_OUTPUT' >&2
    exit 2
fi
binary=${ACPI_POWER_TEST_OUTPUT:-$work/contract}
case "$binary" in /*) ;; *) binary="$PWD/$binary" ;; esac
[ -d "$(dirname -- "$binary")" ] || exit 2
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
# Optional preserved baseline: select its unmodified functions, not a model.
ruby - "${ACPI_POWER_SOURCE:-$src/sys/dev/acpi/acpi_power.c}" "$src" "$work" <<'RUBY'
file, src, out = ARGV
s = File.read(file)
parts = [
  [s.index('/* $NetBSD:'), s.index('#include <sys/cdefs.h>')],
  [s.index('struct acpi_power_res {'), s.index('static int32_t acpi_power_acpinode')],
  [s.index("static int\nacpi_power_res_list_init(") || s.index("static struct acpi_power_res *\nacpi_power_res_init("), s.index("bool\nacpi_power_register(")],
  [s.index("void\nacpi_power_deregister("), s.index("/*\n * Get the D-state")],
  [s.index("static ACPI_STATUS\nacpi_power_switch("), s.index("static ACPI_STATUS\nacpi_power_res_sta(")],
  [s.index("static ACPI_OBJECT *\nacpi_power_pkg_get("), s.index('SYSCTL_SETUP(')]
]
abort 'PowerResource source selection changed' if parts.any? { |a,b| !a || !b || b <= a }
prototypes = s.scan(/^static [^;{]+;/).select { |p| p.match?(/\bacpi_power_res_(init|get|list_init|ref|deref|switch)\s*\(/) || p.match?(/\bacpi_power_pkg_get\s*\(/) }
File.write(File.join(out,'power-native.inc'), prototypes.join("\n")+"\n"+parts.map { |a,b| s[a...b] }.join("\n"))
File.write(File.join(out,'queue-native.h'), File.read(File.join(src,'sys/sys/queue.h')))
RUBY
${CC:-cc} -std=c11 -O2 -g -Wall -Wextra -Werror ${CFLAGS:-} -pthread \
    -I"$work" "$src/ember/tools/acpi-power-contract.c" -o "$binary"
if [ "${ACPI_POWER_ANALYZE:-0}" = 1 ]; then
    # Predefined macros describe the actual host compiler configuration.
    ${CC:-cc} -std=c11 ${CFLAGS:-} -dM -E -x c /dev/null \
        > "$binary.gcc-predefined.h"
    cpp_status=0
    cppcheck --quiet --check-level=exhaustive \
        --enable=warning,performance,portability --error-exitcode=1 \
        --language=c --std=c11 --platform=unix64 \
        --include="$binary.gcc-predefined.h" -I"$work" \
        "$src/ember/tools/acpi-power-contract.c" \
        > "$binary.cppcheck.log" 2>&1 || cpp_status=$?
    cat "$binary.cppcheck.log"
    csa_status=0
    ${CLANG:-clang} --analyze -std=c11 -pthread -I"$work" \
        -Xanalyzer -analyzer-output=text \
        "$src/ember/tools/acpi-power-contract.c" -o "$work/csa.plist" \
        > "$binary.csa.log" 2>&1 || csa_status=$?
    cat "$binary.csa.log"
    warnings=$(awk '/warning:/ { n++ } END { print n+0 }' "$binary.csa.log")
    printf 'ACPI power analysis: Cppcheck exit=%s, CSA exit=%s, CSA diagnostics=%s\n' \
        "$cpp_status" "$csa_status" "$warnings"
    # Retain inherited findings. A warning-bearing analysis is not clean.
    [ "$cpp_status" = 0 ] && [ "$csa_status" = 0 ] && [ "$warnings" = 0 ] || exit 1
fi
[ "${COMPILE_ONLY:-0}" = 0 ] || exit 0
if [ -n "${TEST_RUNNER:-}" ]; then
    # One executable path. A wrapper owns any Valgrind or runner options.
    "$TEST_RUNNER" "$binary"
else
    "$binary"
fi
