#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: original actual-source CPU-only VPU preparation contract.
set -eu
[ "$#" -eq 0 ] || exit 2
case ${CIXVPU_PREPARE_ANALYZE:-0} in 0|1) ;; *) exit 2 ;; esac
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/cixvpu-prepare.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
api=-DCIXVPU_DMA_API
for part in source header; do
    case "$part" in source) ext=c;; header) ext=h;; esac
    ruby -e 'File.write(ARGV[1],File.readlines(ARGV[0]).reject { |s| s.start_with?("#include ") }.join)' \
        "$src/sys/dev/ic/cixvpu_dma.$ext" "$work/dma-$part.inc"
done
source=${CIXVPU_PREPARE_SOURCE:-"$src/sys/dev/ic/cixvpu_prepare.c"}
if [ -f "$source" ]; then
    api="$api -DCIXVPU_PREPARE_API"
    for part in source header; do
        case "$part" in source) ext=c;; header) ext=h;; esac
        case "$part" in source) input=$source;; header) input=$src/sys/dev/ic/cixvpu_prepare.h;; esac
        ruby -e 'File.write(ARGV[1],File.readlines(ARGV[0]).reject { |s| s.start_with?("#include ") }.join)' \
            "$input" "$work/prepare-$part.inc"
    done
fi
# CFLAGS is an intentional compiler argument list.
# shellcheck disable=SC2086
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} $api \
    -I"$src/sys/dev/ic" -I"$work" "$src/ember/tools/cixvpu-prepare-contract.c" \
    "$src/sys/dev/ic/cixvpu_fw.c" "$src/sys/dev/ic/cixvpu_fw_plan.c" \
    "$src/sys/dev/ic/cixvpu_mmu.c" -o "$work/test"
if [ -n "${CIXVPU_PREPARE_TEST_OUTPUT:-}" ]; then cp "$work/test" "$CIXVPU_PREPARE_TEST_OUTPUT"; fi
if [ "${CIXVPU_PREPARE_ANALYZE:-0}" = 1 ]; then
    ${CC:-cc} -std=c11 -dM -E -x c /dev/null > "$work/predefined.h"
    cppcheck --quiet --check-level=exhaustive --enable=warning,performance,portability \
        --error-exitcode=1 --include="$work/predefined.h" --std=c11 --platform=unix64 \
        $api -I"$work" -I"$src/sys/dev/ic" "$src/ember/tools/cixvpu-prepare-contract.c"
    set --
    if [ "$(uname -s)" = Darwin ]; then set -- -isysroot "$SDKROOT"; fi
    if ! "${CSA:-clang}" --analyze -std=c11 -Wall -Wextra -Werror $api \
        -I"$work" -I"$src/sys/dev/ic" "$@" "$src/ember/tools/cixvpu-prepare-contract.c" \
        -o "$work/prepare.plist" 2> "$work/csa.log"; then
        cat "$work/csa.log"; exit 1
    fi
    cat "$work/csa.log"
    test ! -s "$work/csa.log"
fi
if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/test"; else "$work/test"; fi
