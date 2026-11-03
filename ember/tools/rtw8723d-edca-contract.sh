#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; AI-assisted RTL8723D EDCA behavioral checks.
set -eu
: "${CC:?select CC using the SDK run-native-check command}"
[ "$#" -eq 0 ] || { echo "usage: $0" >&2; exit 2; }
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/rtw8723d-edca.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work/dev/ic"
for header in rtw88_hw.h rtw88_io.h rtw8723d_edca.h; do
    ln -s "$src/sys/dev/ic/$header" "$work/dev/ic/$header"
done
{
    printf "#include <stdbool.h>\n#include <stddef.h>\n#include <stdint.h>\n"
    sed -n "/^struct rtw88_dma {/,/^};/p; /^struct rtw88_bus_ops {/,/^};/p" "$src/sys/dev/ic/rtw88var.h"
} > "$work/dev/ic/rtw88var.h"
"$CC" -std=c11 -Wall -Wextra -Werror ${RTW88_TEST_CFLAGS:-} -I"$work" \
    "$tools/rtw8723d-edca-contract.c" "$src/sys/dev/ic/rtw8723d_edca.c" \
    "$src/sys/dev/ic/rtw88_hw.c" -o "$work/test"
if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/test"; else "$work/test"; fi
