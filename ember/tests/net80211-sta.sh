#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/net80211-sta.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
${CC:-cc} -std=c99 -Wall -Wextra -Werror ${STA_TEST_CFLAGS:-${CFLAGS:-}} \
    "$src/sys/net80211/tests/sta.c" -o "$work/sta"
if [ -n "${TEST_RUNNER:-}" ]; then
    "$TEST_RUNNER" "$work/sta"
else
    "$work/sta"
fi
