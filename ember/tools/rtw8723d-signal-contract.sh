#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; AI-assisted RTL8723D PHY signal behavioral checks.
set -eu
: "${CC:?select CC using the SDK run-native-check command}"
[ "$#" -eq 0 ] || { echo "usage: $0" >&2; exit 2; }
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/rtw8723d-signal.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work/dev"
ln -s "$src/sys/dev/ic" "$work/dev/ic"
"$CC" -std=c11 -Wall -Wextra -Werror ${RTW88_TEST_CFLAGS:-} -I"$work" \
    "$tools/rtw8723d-signal-contract.c" "$src/sys/dev/ic/rtw8723d_rx_signal.c" -o "$work/test"
if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/test"; else "$work/test"; fi
