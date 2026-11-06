#!/bin/sh
set -eu
: "${CC:?select CC using SDK run-native-check}"
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
wcpu=${1:-$src}
[ "$#" -le 1 ] || { echo "usage: $0 [WCPU_SOURCE]" >&2; exit 2; }
[ -f "$wcpu/sys/dev/ic/rtw88_wcpu.h" ] || { echo 'missing shared WCPU source' >&2; exit 1; }
work=$(mktemp -d "${TMPDIR:-/tmp}/rtw8821c-coex.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work/sys" "$work/dev/ic"
for header in "$src"/sys/dev/ic/*.h; do ln -s "$header" "$work/dev/ic/${header##*/}"; done
[ -e "$work/dev/ic/rtw88_wcpu.h" ] || ln -s "$wcpu/sys/dev/ic/rtw88_wcpu.h" "$work/dev/ic/rtw88_wcpu.h"
for h in param systm; do printf '#include <errno.h>\n#include "rtw88-test-kernel.h"\n' > "$work/sys/$h.h"; done
"$CC" -std=c11 -Wall -Wextra -Werror ${RTW88_TEST_CFLAGS:-} -I"$work" -I"$tools" \
    "$tools/rtw8821c-coex-contract.c" "$src/sys/dev/ic/rtw8821c_coex.c" -o "$work/test"
if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/test"; else "$work/test"; fi

"$CC" -std=c11 -Wall -Wextra -Werror ${RTW88_TEST_CFLAGS:-} -I"$work" -I"$tools" \
    "$tools/rtw8821c-coex-trace-contract.c" "$src/sys/dev/ic/rtw8821c_coex.c" -o "$work/trace-test"
if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/trace-test" "$tools/rtw8821c-coex.trace"; else "$work/trace-test" "$tools/rtw8821c-coex.trace"; fi
