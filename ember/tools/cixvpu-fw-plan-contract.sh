#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors. AI-assisted real-source tests.
set -eu
[ "$#" -le 1 ] || exit 2
case "${CIXVPU_PLAN_ANALYZE:-0}" in 0|1) ;; *) exit 2 ;; esac
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)}
work=$(mktemp -d "${TMPDIR:-/tmp}/cixvpu-fw-plan.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
case "${CIXVPU_COMPILE_ONLY:-0}" in 0|1) ;; *) exit 2 ;; esac
if [ "${CIXVPU_COMPILE_ONLY:-0}" = 1 ] && [ -z "${CIXVPU_PLAN_OUTPUT:-}" ]; then
    echo 'CIXVPU_COMPILE_ONLY requires CIXVPU_PLAN_OUTPUT' >&2
    exit 2
fi
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
# CFLAGS is a caller-supplied compiler argument list.
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -O2 -g -Wall -Wextra -Werror ${CFLAGS:-} \
    -I"$src/sys/dev/ic" "$src/sys/dev/ic/cixvpu_fw.c" \
    "$src/sys/dev/ic/cixvpu_fw_plan.c" "$src/sys/dev/ic/cixvpu_mmu.c" \
    "$src/ember/tools/cixvpu-fw-plan-contract.c" -o "$work/contract"
if [ -n "${CIXVPU_PLAN_OUTPUT:-}" ]; then
    cp "$work/contract" "$CIXVPU_PLAN_OUTPUT"
fi
if [ "${CIXVPU_PLAN_ANALYZE:-0}" = 1 ]; then
    "${CC:-cc}" -std=c11 -dM -E -x c /dev/null > "$work/predefined.h"
    if ! cppcheck --quiet --check-level=exhaustive \
        --enable=warning,performance,portability --error-exitcode=1 \
        --include="$work/predefined.h" --std=c11 --platform=unix64 \
        -I"$src/sys/dev/ic" "$src/sys/dev/ic/cixvpu_fw_plan.c" \
        "$src/ember/tools/cixvpu-fw-plan-contract.c" 2> "$work/cppcheck.log"; then
        cat "$work/cppcheck.log"; exit 1
    fi
    cat "$work/cppcheck.log"
    test ! -s "$work/cppcheck.log"
    for unit in "$src/sys/dev/ic/cixvpu_fw_plan.c" \
        "$src/ember/tools/cixvpu-fw-plan-contract.c"; do
        set --
        if [ "$(uname -s)" = Darwin ]; then set -- -isysroot "$SDKROOT"; fi
        if ! "${CSA:-clang}" --analyze -std=c11 -Wall -Wextra -Werror "$@" \
            -I"$src/sys/dev/ic" "$unit" -o "$work/unit.plist" \
            2> "$work/csa.log"; then
            cat "$work/csa.log"; exit 1
        fi
        cat "$work/csa.log"
        test ! -s "$work/csa.log"
    done
fi
[ "${CIXVPU_COMPILE_ONLY:-0}" = 0 ] || exit 0
if [ -n "${TEST_RUNNER:-}" ]; then
    # One executable path. Its wrapper owns any runner options.
    "$TEST_RUNNER" "$work/contract"
else
    "$work/contract"
fi
