#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; AI-assisted RTL8821C firmware checks.
set -eu
: "${CC:?select CC using SDK run-native-check}"
[ "$#" -eq 0 ] || { echo "usage: $0" >&2; exit 2; }
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/rtw8821c-fw.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
"$CC" -std=c11 -Wall -Wextra -Werror ${RTW88_TEST_CFLAGS:-} \
    -I"$src/sys/dev/ic" "$tools/rtw8821c-fw-contract.c" \
    "$src/sys/dev/ic/rtw88_hw.c" "$src/sys/dev/ic/rtw8821c.c" \
    "$src/sys/dev/ic/rtw8822c_hw.c" -o "$work/test"
run_test() {
    if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$@"; else "$@"; fi
}
run_test "$work/test"
if [ -n "${RTW8821C_FIRMWARE:-}" ]; then
    : "${PYTHON:?select Python using SDK run-native-check}"
    "$PYTHON" - "$RTW8821C_FIRMWARE" <<'PY'
import hashlib, pathlib, sys
p = pathlib.Path(sys.argv[1]).read_bytes()
if len(p) != 139472 or hashlib.sha256(p).hexdigest() != '2ef409bc418549fcf294061dd0cae1fc22fd9da79b60524950b25de18732f3f0':
    raise SystemExit('RTL8821C fixture integrity check failed')
PY
    run_test "$work/test" "$RTW8821C_FIRMWARE"
fi
