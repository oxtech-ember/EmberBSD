#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors. Original actual-source tests.
set -eu
[ "$#" -le 1 ] || exit 2
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)}
work=$(mktemp -d "${TMPDIR:-/tmp}/cixvpu-command.XXXXXXXX")
trap 'rm -rf "$work"' 0 HUP INT TERM
case ${CIXVPU_COMMAND_ANALYZE:-0} in 0|1) ;; *) exit 2 ;; esac
case ${CIXVPU_COMPILE_ONLY:-0} in 0|1) ;; *) exit 2 ;; esac
if [ "${CIXVPU_COMPILE_ONLY:-0}" = 1 ] && [ -z "${CIXVPU_COMMAND_OUTPUT:-}" ]; then
    echo 'CIXVPU_COMPILE_ONLY requires CIXVPU_COMMAND_OUTPUT' >&2
    exit 2
fi
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
# CFLAGS is an explicit caller-supplied compiler argument list.
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -O2 -g -Wall -Wextra -Werror ${CFLAGS:-} \
    -I"$src/sys/dev/ic" "$src/sys/dev/ic/cixvpu_command.c" \
    "$src/sys/dev/ic/cixvpu_ring.c" "$src/ember/tools/cixvpu-command-contract.c" \
    -o "$work/contract"
if [ "${CIXVPU_COMMAND_ANALYZE:-0}" = 1 ]; then
    "${CC:-cc}" -std=c11 -dM -E -x c /dev/null > "$work/predefined.h"
    for unit in "$src/sys/dev/ic/cixvpu_command.c" \
        "$src/ember/tools/cixvpu-command-contract.c"; do
        if ! cppcheck --quiet --check-level=exhaustive \
            --enable=warning,performance,portability --error-exitcode=1 \
            --include="$work/predefined.h" --std=c11 --platform=unix64 \
            -I"$src/sys/dev/ic" "$unit" 2> "$work/cppcheck.log"; then
            cat "$work/cppcheck.log" >&2; exit 1
        fi
        cat "$work/cppcheck.log"; test ! -s "$work/cppcheck.log"
        set --
        if [ "$(uname -s)" = Darwin ]; then set -- -isysroot "$SDKROOT"; fi
        if ! "${CSA:-clang}" --analyze -std=c11 -Wall -Wextra -Werror \
            -Xanalyzer -analyzer-werror -I"$src/sys/dev/ic" "$@" "$unit" \
            -o "$work/analysis.plist" 2> "$work/csa.log"; then
            cat "$work/csa.log" >&2; exit 1
        fi
        cat "$work/csa.log"; test ! -s "$work/csa.log"
    done
fi
if [ -n "${CIXVPU_COMMAND_OUTPUT:-}" ]; then
    cp "$work/contract" "$CIXVPU_COMMAND_OUTPUT"
fi
[ "${CIXVPU_COMPILE_ONLY:-0}" = 0 ] || exit 0
if [ -n "${TEST_RUNNER:-}" ]; then
    "$TEST_RUNNER" "$work/contract"
else
    "$work/contract"
fi
