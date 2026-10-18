#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: original actual-source CIX VPU control runner, AI-assisted.
set -eu
[ "$#" -eq 0 ] || exit 2
analyze=${CIXVPU_CONTROL_ANALYZE-0}
compile_only=${CIXVPU_CONTROL_COMPILE_ONLY-0}
case "$analyze" in 0|1) ;; *) exit 2;; esac
case "$compile_only" in 0|1) ;; *) exit 2;; esac
if [ "$compile_only" = 1 ] && [ -z "${CIXVPU_CONTROL_TEST_OUTPUT:-}" ]; then
    echo 'compile-only requires CIXVPU_CONTROL_TEST_OUTPUT' >&2
    exit 2
fi
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/cixvpu-control.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
api=
if [ -f "$src/sys/dev/acpi/cixvpu_acpi_control.c" ]; then
    api="-DCIXVPU_CONTROL_API -DCIXVPU_ATTACH_API"
    for part in source header; do
        case "$part" in source) ext=c;; header) ext=h;; esac
        ruby -e 'File.write(ARGV[1],File.readlines(ARGV[0]).reject { |s| s.start_with?("#include ") }.join)' \
            "$src/sys/dev/acpi/cixvpu_acpi_control.$ext" "$work/control-$part.inc"
    done
fi
ruby -e 'File.write(ARGV[1],File.readlines(ARGV[0]).reject { |s| s.start_with?("#include ") }.join)' \
    "$src/sys/dev/acpi/cixvpu_acpi.c" "$work/attachment-source.inc"
ruby -e 'File.write(ARGV[1],File.readlines(ARGV[0]).reject { |s| s.start_with?("#include ") }.join)' \
    "$src/sys/dev/ic/cixvpu_dma.h" "$work/dma-header.inc"
ruby -e 'File.write(ARGV[1],File.readlines(ARGV[0]).reject { |s| s.start_with?("#include ") }.join)' \
    "$src/sys/dev/ic/cixvpu_prepare.h" "$work/prepare-header.inc"
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} $api \
    -isystem "$src/sys/external/bsd/acpica/dist/include" -I"$src/sys/dev/ic" -I"$work" \
    "$src/ember/tools/cixvpu-control-contract.c" "$src/sys/dev/ic/cixvpu_resources.c" \
    "$src/sys/dev/ic/cixvpu_fw.c" "$src/sys/dev/ic/cixvpu_fw_plan.c" -o "$work/test"
if [ -n "${CIXVPU_CONTROL_TEST_OUTPUT:-}" ]; then cp "$work/test" "$CIXVPU_CONTROL_TEST_OUTPUT"; fi
if [ "$analyze" = 1 ]; then
    ${CC:-cc} -std=c11 -dM -E -x c /dev/null > "$work/predefined.h"
    cppcheck --quiet --check-level=exhaustive --enable=warning,performance,portability \
        --error-exitcode=1 --include="$work/predefined.h" --std=c11 --platform=unix64 \
        -DACPI_APPLICATION $api -I"$work" -I"$src/sys/dev/ic" \
        -I"$src/sys/external/bsd/acpica/dist/include" "$src/ember/tools/cixvpu-control-contract.c"
    set --
    if [ "$(uname -s)" = Darwin ]; then set -- -isysroot "$SDKROOT"; fi
    "${CSA:-clang}" --analyze -std=c11 -Wall -Wextra -Werror \
        -DACPI_APPLICATION $api -isystem "$src/sys/external/bsd/acpica/dist/include" \
        -I"$work" -I"$src/sys/dev/ic" "$@" "$src/ember/tools/cixvpu-control-contract.c" \
        -o "$work/control.plist" 2> "$work/csa.log"
    cat "$work/csa.log"
    test ! -s "$work/csa.log"
fi
if [ "$compile_only" = 1 ]; then exit 0; fi
if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/test"; else "$work/test"; fi
