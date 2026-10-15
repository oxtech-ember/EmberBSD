#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD native RTL8822C firmware contracts, AI-assisted.
set -eu
# Homebrew GCC can retain a removed Command Line Tools SDK as its default.
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path)
    export SDKROOT
fi
[ "$#" -eq 0 ] || { echo "usage: $0" >&2; exit 2; }
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/rtw88-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

${CC:-cc} -std=c11 -Wall -Wextra -Werror ${RTW88_TEST_CFLAGS:-} \
    -I"$src/sys/dev/ic" "$tools/rtw88-contract.c" \
    "$src/sys/dev/ic/rtw88_hw.c" -o "$work/rtw88-contract"
"$work/rtw88-contract"

# Optional offline real fixtures. Always verify both before reading them.
if [ -n "${RTW88_FIXTURE_DIR:-}" ]; then
    sha256_file()
    {
        if command -v sha256 >/dev/null 2>&1; then sha256 -q "$1"
        elif command -v sha256sum >/dev/null 2>&1; then
            sha256sum "$1" | awk '{print $1}'
        else shasum -a 256 "$1" | awk '{print $1}'; fi
    }
    [ "$(sha256_file "$RTW88_FIXTURE_DIR/rtw8822c_fw.bin")" = \
        36b483e37640359a6c686be4661b5f2d94928f93e126a2de09e67c5e04c01b26 ]
    [ "$(sha256_file "$RTW88_FIXTURE_DIR/rtw8822b_fw.bin")" = \
        378ff7b43ae7da18a0311175abc351a1758d25ce553b44f3c5694758efbea84c ]
    "$work/rtw88-contract" "$RTW88_FIXTURE_DIR/rtw8822c_fw.bin" \
        "$RTW88_FIXTURE_DIR/rtw8822b_fw.bin"
else
    echo 'Real fixtures not selected; see sys/dev/ic/rtw88-provenance.md.'
fi
