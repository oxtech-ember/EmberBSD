#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: original actual-source stopped-only DMA runner, AI-assisted.
set -eu
[ "$#" -eq 0 ] || exit 2
analyze=${CIXVPU_DMA_ANALYZE-0}
compile_only=${CIXVPU_DMA_COMPILE_ONLY-0}
case "$analyze" in 0|1) ;; *) exit 2;; esac
case "$compile_only" in 0|1) ;; *) exit 2;; esac
if [ "$compile_only" = 1 ] && [ -z "${CIXVPU_DMA_TEST_OUTPUT:-}" ]; then
    echo 'compile-only requires CIXVPU_DMA_TEST_OUTPUT' >&2
    exit 2
fi
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/cixvpu-dma.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
api=
if [ -f "$src/sys/dev/ic/cixvpu_dma.c" ]; then
    api=-DCIXVPU_DMA_API
    for part in source header; do
        case "$part" in source) ext=c;; header) ext=h;; esac
        ruby -e 'File.write(ARGV[1],File.readlines(ARGV[0]).reject { |s| s.start_with?("#include ") }.join)' \
            "$src/sys/dev/ic/cixvpu_dma.$ext" "$work/dma-$part.inc"
    done
    if [ -f "$src/sys/dev/ic/cixvpu_dma_private.h" ]; then
        ruby -e 'p=ARGV[0]; h=File.readlines(ARGV[1]).reject { |s| s.start_with?("#include ") }.join; File.write(p,h+File.read(p))' \
            "$work/dma-source.inc" "$src/sys/dev/ic/cixvpu_dma_private.h"
    fi
fi
# CFLAGS is an intentional compiler argument list.
# shellcheck disable=SC2086
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} $api \
    -I"$src/sys/dev/ic" -I"$work" "$src/ember/tools/cixvpu-dma-contract.c" \
    "$src/sys/dev/ic/cixvpu_fw.c" "$src/sys/dev/ic/cixvpu_fw_plan.c" -o "$work/test"
if [ -n "${CIXVPU_DMA_TEST_OUTPUT:-}" ]; then cp "$work/test" "$CIXVPU_DMA_TEST_OUTPUT"; fi
if [ "$analyze" = 1 ]; then
    ${CC:-cc} -std=c11 -dM -E -x c /dev/null > "$work/predefined.h"
    cppcheck --quiet --check-level=exhaustive --enable=warning,performance,portability \
        --error-exitcode=1 --include="$work/predefined.h" --std=c11 --platform=unix64 \
        $api -I"$work" -I"$src/sys/dev/ic" "$src/ember/tools/cixvpu-dma-contract.c"
    set --
    if [ "$(uname -s)" = Darwin ]; then set -- -isysroot "$SDKROOT"; fi
    "${CSA:-clang}" --analyze -std=c11 -Wall -Wextra -Werror $api \
        -I"$work" -I"$src/sys/dev/ic" "$@" "$src/ember/tools/cixvpu-dma-contract.c" \
        -o "$work/dma.plist" 2> "$work/csa.log"
    cat "$work/csa.log"
    test ! -s "$work/csa.log"
fi
[ "$compile_only" = 0 ] || exit 0
if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/test"; else "$work/test"; fi
