#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: actual-source normal PHY table graph runner, AI-assisted.
set -eu
[ "$#" -eq 0 ] || exit 2
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/rtw8822c-init-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
mkdir -p "$work/include/dev"
ln -s "$src/sys/dev/ic" "$work/include/dev/ic"
# Compile the actual caller unchanged. Table decoding and MMIO are boundaries.
ruby - "$src/sys/dev/ic/rtw8822c_boot.c" "$work/body.c" <<'RUBY'
lines = File.readlines(ARGV[0])
first = lines.index { |line| line.start_with?('rtw8822c_load_tables(') }
abort 'Normal table caller not found' unless first && first.positive?
last = ((first + 1)...lines.length).find { |index| lines[index] == "}\n" }
abort 'Normal table caller is incomplete' unless last
File.write(ARGV[1], lines[(first - 1)..last].join)
RUBY
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${RTW88_TEST_CFLAGS:-} \
    -I"$work/include" -I"$work" "$tools/rtw8822c-init-contract.c" -o "$work/test"
if [ -n "${TEST_RUNNER:-}" ]; then
    "$TEST_RUNNER" "$work/test" "$tools/rtw8822c-init.trace"
else
    "$work/test" "$tools/rtw8822c-init.trace"
fi
