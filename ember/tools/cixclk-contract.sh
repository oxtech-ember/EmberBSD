#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD actual-source CIX clock contract runner, AI-assisted.
set -eu
[ "$#" -eq 0 ] || exit 2
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/cixclk-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
source=${CIXCLK_SOURCE:-"$src/sys/dev/acpi/cixclk_acpi.c"}
api=
if [ -f "$source" ]; then
    api=-DCIXCLK_DRIVER_API
    ruby -e 'File.write(ARGV[1],File.readlines(ARGV[0]).reject { |l| l.start_with?("#include ") }.join)' \
        "$source" "$work/cixclk-source.inc"
fi
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} $api \
    -DACPI_MISALIGNMENT_NOT_SUPPORTED -isystem "$src/sys/external/bsd/acpica/dist/include" \
    -I"$work" "$src/ember/tools/cixclk-contract.c" -o "$work/test"
if [ -n "${CIXCLK_TEST_OUTPUT:-}" ]; then cp "$work/test" "$CIXCLK_TEST_OUTPUT"; fi
if [ "${CIXCLK_COMPILE_ONLY:-0}" = 1 ]; then exit 0; fi
if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/test"; else "$work/test"; fi
