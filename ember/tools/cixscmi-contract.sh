#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD actual-source CIX SCMI binding contract runner, AI-assisted.
set -eu
[ "$#" -eq 0 ] || exit 2
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/cixscmi-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
mkdir -p "$work/dev/ic"
cp "$src/sys/dev/ic/scmi_smt_diag.h" "$work/dev/ic/scmi_smt_diag.h"
source=${CIXSCMI_SOURCE:-"$src/sys/dev/acpi/cixscmi_acpi.c"}
api=
if [ -f "$source" ]; then
    api=-DCIXSCMI_DRIVER_API
    ruby -e 'File.write(ARGV[1],File.readlines(ARGV[0]).reject { |l| l.start_with?("#include ") }.join)' \
        "$source" "$work/cixscmi-source.inc"
fi
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} $api -pthread \
    -DACPI_MISALIGNMENT_NOT_SUPPORTED -isystem "$src/sys/external/bsd/acpica/dist/include" \
    -I"$work" "$src/ember/tools/cixscmi-contract.c" \
    "$src/sys/dev/ic/scmi_smt_diag.c" -o "$work/test"
if [ -n "${CIXSCMI_TEST_OUTPUT:-}" ]; then cp "$work/test" "$CIXSCMI_TEST_OUTPUT"; fi
if [ "${CIXSCMI_ANALYZE:-0}" = 1 ]; then
    ${CC:-cc} -std=c11 -pthread -dM -E -x c /dev/null > "$work/predefined.h"
    cppcheck --quiet --check-level=exhaustive \
        --enable=warning,performance,portability --error-exitcode=1 \
        --include="$work/predefined.h" --std=c11 --platform=unix64 \
        -DACPI_APPLICATION -DACPI_MISALIGNMENT_NOT_SUPPORTED $api -I"$work" \
        -I"$src/sys/external/bsd/acpica/dist/include" \
        "$src/ember/tools/cixscmi-contract.c"
    set --
    if [ "$(uname -s)" = Darwin ]; then set -- -isysroot "$SDKROOT"; fi
    "${CSA:-clang}" --analyze -std=c11 -pthread -Wall -Wextra -Werror \
        -DACPI_APPLICATION -DACPI_MISALIGNMENT_NOT_SUPPORTED $api \
        -isystem "$src/sys/external/bsd/acpica/dist/include" -I"$work" \
        "$@" "$src/ember/tools/cixscmi-contract.c" -o "$work/binding.plist" \
        2> "$work/csa.log"
    cat "$work/csa.log"
    test ! -s "$work/csa.log"
fi
if [ "${CIXSCMI_COMPILE_ONLY:-0}" = 1 ]; then exit 0; fi
if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/test"; else "$work/test"; fi
