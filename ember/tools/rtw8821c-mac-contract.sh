#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD RTL8821C MAC init contract runner, AI-assisted.
set -eu
# TEST_RUNNER names one executable, for example a Memcheck wrapper.
run_test() {
    if [ -n "${TEST_RUNNER:-}" ]; then
        "$TEST_RUNNER" "$@"
    else
        "$@"
    fi
}
[ "$#" -eq 0 ] || { echo "usage: $0" >&2; exit 2; }
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/rtw8821c-mac-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
: "${CC:?select CC using SDK run-native-check}"
mkdir -p "$work/include/sys" "$work/include/dev"
ln -s "$src/sys/dev/ic" "$work/include/dev/ic"
for header in sys/param.h sys/systm.h; do
    printf '#include <errno.h>\n#include "rtw88-test-kernel.h"\n' > "$work/include/$header"
done
"$CC" -std=c11 -Wall -Wextra -Werror ${RTW88_TEST_CFLAGS:-} \
    -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" \
    "$tools/rtw8821c-mac-contract.c" \
    "$src/sys/dev/ic/rtw8821c_mac.c" \
    -o "$work/test"
run_test "$work/test" "${RTW8821C_TRACE:-$tools/rtw8821c-mac.trace}"
