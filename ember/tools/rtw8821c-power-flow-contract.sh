#!/bin/sh
set -eu
: "${CC:?select CC using SDK run-native-check}"
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/rtw8821c-boot.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work/sys" "$work/dev"
ln -s "$src/sys/dev/ic" "$work/dev/ic"
for h in param systm; do printf '#include <errno.h>\n#include "rtw88-test-kernel.h"\n' > "$work/sys/$h.h"; done
"$CC" -std=c11 -Wall -Wextra -Werror ${RTW88_TEST_CFLAGS:-} -I"$work" -I"$tools" \
    "$tools/rtw8821c-power-flow-contract.c" "$src/sys/dev/ic/rtw8821c_power.c" -o "$work/test"
if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/test"; else "$work/test"; fi
