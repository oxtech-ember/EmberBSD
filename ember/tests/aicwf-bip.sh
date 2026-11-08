#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Host BIP contract. OpenSSL supplies independent test cryptography only.
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/aicwf-bip.XXXXXXXX")
if [ "${AICWF_BIP_KEEP:-0}" = 1 ]; then
    printf 'Test directory: %s\n' "$work"
else
    trap 'rm -rf "$work"' EXIT HUP INT TERM
fi
# Intentional splitting: compiler and pkg-config outputs are argument lists.
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${AICWF_BIP_CFLAGS:-} \
    $(${PKG_CONFIG:-pkg-config} --cflags libcrypto) \
    -idirafter "$src/sys" -I"${AICWF_BIP_INCLUDE:-$src/sys/dev/sdmmc}" \
    "$src/ember/tools/aicwf-bip-contract.c" \
    $(${PKG_CONFIG:-pkg-config} --libs libcrypto) -o "$work/check"
# A sanitizer finding must fail the run, including in boundary tests.
ASAN_OPTIONS="${ASAN_OPTIONS:+$ASAN_OPTIONS:}halt_on_error=1"
UBSAN_OPTIONS="${UBSAN_OPTIONS:+$UBSAN_OPTIONS:}halt_on_error=1"
export ASAN_OPTIONS UBSAN_OPTIONS
if [ -n "${TEST_RUNNER:-}" ]; then
    "$TEST_RUNNER" "$work/check"
else
    "$work/check"
fi
if [ "${AICWF_BIP_ANALYZE:-0}" = 1 ]; then
    "${CSA:-clang}" --analyze -std=c11 -Wall -Wextra -Werror \
        -Xanalyzer -analyzer-werror \
        $(${PKG_CONFIG:-pkg-config} --cflags libcrypto) \
        -idirafter "$src/sys" -I"${AICWF_BIP_INCLUDE:-$src/sys/dev/sdmmc}" \
        "$src/ember/tools/aicwf-bip-contract.c" -o "$work/analysis.plist"
    cppcheck --enable=warning,style,performance,portability \
        --error-exitcode=1 --std=c11 --library=posix \
        -I"$src/sys" -I"${AICWF_BIP_INCLUDE:-$src/sys/dev/sdmmc}" \
        "$src/ember/tools/aicwf-bip-contract.c"
fi
