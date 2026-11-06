#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: actual-source PCI3081 MAC power setup runner, AI-assisted.
set -eu
: "${CC:?select CC using SDK run-native-check}"
[ "$#" -eq 0 ] || exit 2
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/rtw8821c-power-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work/include/sys" "$work/include/dev"
ln -s "$src/sys/dev/ic" "$work/include/dev/ic"
for header in sys/param.h sys/systm.h; do
    printf '#include <errno.h>\n#include "rtw88-test-kernel.h"\n' > "$work/include/$header"
done
"$CC" -std=c11 -Wall -Wextra -Werror ${RTW88_TEST_CFLAGS:-} \
    -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" \
    "$tools/rtw8821c-power-contract.c" \
    "${RTW8821C_POWER_SOURCE:-$src/sys/dev/ic/rtw8821c_power.c}" -o "$work/test"
if [ -n "${TEST_RUNNER:-}" ]; then
    "$TEST_RUNNER" "$work/test" "$tools/rtw8822c-power.trace"
else
    "$work/test" "$tools/rtw8822c-power.trace"
fi
