#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: original safe actual-source shutdown contract runner, AI-assisted.
set -eu
ulimit -c 0
[ "$#" -eq 0 ] || exit 2
part_filter=${IMMEDIATE_PART:-all}
case "$part_filter" in all|shutdown|reboot) ;; *) exit 2;; esac
analyze=${IMMEDIATE_ANALYZE:-0}
compile_only=${IMMEDIATE_COMPILE_ONLY:-0}
case "$analyze" in 0|1) ;; *) exit 2;; esac
case "$compile_only" in 0|1) ;; *) exit 2;; esac
if [ "$compile_only" = 1 ] && [ -z "${IMMEDIATE_TEST_OUTPUT:-}" ]; then
    echo 'compile-only requires IMMEDIATE_TEST_OUTPUT prefix' >&2
    exit 2
fi
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/immediate-shutdown.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
for part in shutdown reboot; do
    [ "$part_filter" = all ] || [ "$part_filter" = "$part" ] || continue
    ruby -e 'File.write(ARGV[1],File.readlines(ARGV[0]).reject { |s| s.start_with?("#include ") }.join)' \
        "$src/sbin/$part/$part.c" "$work/native-source.inc"
    define=
    if [ "$part" = shutdown ]; then define=-DTEST_SHUTDOWN; fi
    # CFLAGS is a caller-supplied compiler argument list.
    # shellcheck disable=SC2086
    # Match native ARGSUSED parameter handling; do not edit the included functions.
    ${CC:-cc} -std=c11 -Wall -Wextra -Wno-unused-parameter -Werror ${CFLAGS:-} $define -I"$work" \
        "$src/ember/tools/immediate-shutdown-contract.c" -o "$work/$part-test"
    if [ -n "${IMMEDIATE_TEST_OUTPUT:-}" ]; then cp "$work/$part-test" "$IMMEDIATE_TEST_OUTPUT-$part"; fi
    if [ "$analyze" = 1 ]; then
        ${CC:-cc} -std=c11 -dM -E -x c /dev/null > "$work/predefined.h"
        cppcheck --quiet --check-level=exhaustive --enable=warning,performance,portability \
            --error-exitcode=1 --include="$work/predefined.h" --std=c11 --platform=unix64 \
            $define -I"$work" "$src/ember/tools/immediate-shutdown-contract.c"
        set --
        if [ "$(uname -s)" = Darwin ]; then set -- -isysroot "$SDKROOT"; fi
        "${CSA:-clang}" --analyze -std=c11 -Wall -Wextra -Wno-unused-parameter -Werror \
            $define -I"$work" "$@" "$src/ember/tools/immediate-shutdown-contract.c" \
            -o "$work/$part.plist" 2> "$work/csa.log"
        cat "$work/csa.log"
        test ! -s "$work/csa.log"
    fi
    if [ "$compile_only" = 0 ]; then
        if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/$part-test"; else "$work/$part-test"; fi
    fi
done
