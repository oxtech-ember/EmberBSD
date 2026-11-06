#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
[ "$#" -le 1 ] || exit 2
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=${1:-$(CDPATH= cd -- "$tools/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/rtw88-rxba.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
python3 "$tools/rtw88-rxba-lifecycle-contract.py" "$src/sys/dev/ic/rtw88_net.c" > "$work/test.c"
"${CC:?selected CC required}" -std=c11 -Wall -Wextra -Werror ${RTW88_TEST_CFLAGS:-} "$work/test.c" -o "$work/test"
if [ -n "${TEST_RUNNER:-}" ]; then
    "$TEST_RUNNER" "$work/test"
else
    "$work/test"
fi
echo 'RX BA lifecycle contract PASS'
