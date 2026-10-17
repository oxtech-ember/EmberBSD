#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: original actual-source reset runner, AI-assisted.
set -eu
[ "$#" -eq 0 ] || exit 2
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/cixreset-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
api=
if [ -f "$src/sys/dev/acpi/cixreset_acpi.c" ]; then
    api=-DCIXRESET_DRIVER_API
    if grep -q cixreset_acpi_get_state "$src/sys/dev/acpi/cixreset_acpi.c"; then
        api="$api -DCIXRESET_STATE_API"
    fi
    # Compile every production function intact; remove kernel includes only.
    ruby -e 'File.write(ARGV[1],File.readlines(ARGV[0]).reject { |s| s.start_with?("#include ") }.join)' \
      "$src/sys/dev/acpi/cixreset_acpi.c" "$work/cixreset-source.inc"
    ruby -e 'File.write(ARGV[1],File.readlines(ARGV[0]).reject { |s| s.start_with?("#include ") }.join)' \
      "$src/sys/dev/acpi/cixreset_acpi.h" "$work/cixreset-header.inc"
fi
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} $api -pthread \
    -isystem "$src/sys/external/bsd/acpica/dist/include" -I"$work" \
    "$src/ember/tools/cixreset-contract.c" -o "$work/test"
if [ -n "${CIXRESET_TEST_OUTPUT:-}" ]; then cp "$work/test" "$CIXRESET_TEST_OUTPUT"; fi
if [ "${CIXRESET_ANALYZE:-0}" = 1 ]; then
    ${CC:-cc} -std=c11 -dM -E -x c /dev/null > "$work/predefined.h"
    cppcheck --quiet --check-level=exhaustive \
        --enable=warning,performance,portability --error-exitcode=1 \
        --include="$work/predefined.h" --std=c11 --platform=unix64 \
        -DACPI_APPLICATION $api -I"$work" \
        -I"$src/sys/external/bsd/acpica/dist/include" \
        "$src/ember/tools/cixreset-contract.c"
    set --
    if [ "$(uname -s)" = Darwin ]; then set -- -isysroot "$SDKROOT"; fi
    "${CSA:-clang}" --analyze -std=c11 -Wall -Wextra -Werror \
        -DACPI_APPLICATION $api -pthread \
        -isystem "$src/sys/external/bsd/acpica/dist/include" -I"$work" \
        "$@" "$src/ember/tools/cixreset-contract.c" -o "$work/reset.plist" \
        2> "$work/csa.log"
    cat "$work/csa.log"
    test ! -s "$work/csa.log"
fi
if [ "${CIXRESET_COMPILE_ONLY:-0}" = 1 ]; then exit 0; fi
if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/test"; else "$work/test"; fi
