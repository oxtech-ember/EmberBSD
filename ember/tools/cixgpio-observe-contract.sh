#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Actual closed attachment functions; native service models only.
set -eu
[ "$#" -eq 0 ] || exit 2
case ${CIXGPIOOBS_ANALYZE:-0} in 0|1) ;; *) exit 2 ;; esac
case ${CIXGPIOOBS_COMPILE_ONLY:-0} in 0|1) ;; *) exit 2 ;; esac
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/cixgpio-observe.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
api=
source=${CIXGPIOOBS_SOURCE:-"$src/sys/dev/acpi/cixgpio_observe_acpi.c"}
if [ -f "$source" ]; then
    api=-DCIXGPIOOBS_API
    ruby -e 'File.write(ARGV[1],File.readlines(ARGV[0]).reject { |s| s.start_with?("#include ") }.join)' \
        "$source" "$work/cixgpio-observe-source.inc"
fi
if [ -n "${CIXGPIOOBS_ARTIFACT_DIR:-}" ] && [ -n "$api" ]; then
    mkdir -p "$CIXGPIOOBS_ARTIFACT_DIR"
    cp "$work/cixgpio-observe-source.inc" "$CIXGPIOOBS_ARTIFACT_DIR/"
fi
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} $api \
    -isystem "$src/sys/external/bsd/acpica/dist/include" \
    -I"$work" "$src/ember/tools/cixgpio-observe-contract.c" -o "$work/test"
if [ -n "${CIXGPIOOBS_TEST_OUTPUT:-}" ]; then cp "$work/test" "$CIXGPIOOBS_TEST_OUTPUT"; fi
if [ "${CIXGPIOOBS_ANALYZE:-0}" = 1 ]; then
    ${CC:-cc} -std=c11 -dM -E -x c /dev/null > "$work/predefined.h"
    set --
    # Cppcheck's unix64 preset defines Linux. Match the actual Darwin compiler.
    if [ "$(uname -s)" = Darwin ]; then set -- -U__linux__; fi
    if ! cppcheck --quiet --check-level=exhaustive \
        --enable=warning,performance,portability --error-exitcode=1 \
        --include="$work/predefined.h" --std=c11 --platform=unix64 $api \
        -DACPI_APPLICATION "$@" -I"$work" -I"$src/sys/external/bsd/acpica/dist/include" \
        "$src/ember/tools/cixgpio-observe-contract.c" 2> "$work/cppcheck.log"; then
        cat "$work/cppcheck.log"; exit 1
    fi
    cat "$work/cppcheck.log"
    test ! -s "$work/cppcheck.log"
    set --
    if [ "$(uname -s)" = Darwin ]; then set -- -isysroot "$SDKROOT"; fi
    if ! "${CSA:-clang}" --analyze -std=c11 -Wall -Wextra -Werror $api \
        -isystem "$src/sys/external/bsd/acpica/dist/include" -I"$work" "$@" \
        "$src/ember/tools/cixgpio-observe-contract.c" \
        -o "$work/csa.plist" 2> "$work/csa.log"; then
        cat "$work/csa.log"; exit 1
    fi
    cat "$work/csa.log"
    test ! -s "$work/csa.log"
fi
if [ "${CIXGPIOOBS_COMPILE_ONLY:-0}" = 1 ]; then exit 0; fi
if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/test"; else "$work/test"; fi
