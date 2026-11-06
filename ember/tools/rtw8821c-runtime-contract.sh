#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# RTL8821CE runtime lifecycle integration with synthetic hardware stages.
set -eu
: "${CC:?select CC using the SDK run-native-check command}"
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
analyze=0
while [ "$#" -gt 0 ]; do
    case "$1" in
        --source) [ "$#" -ge 2 ] || exit 2; src=$(CDPATH= cd -- "$2" && pwd); shift 2 ;;
        --analyze) analyze=1; shift ;;
        *) echo "usage: $0 [--source OS_ROOT] [--analyze]" >&2; exit 2 ;;
    esac
done
work=$(mktemp -d "${TMPDIR:-/tmp}/rtw8821c-runtime.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work/include/sys" "$work/include/dev" "$work/sys/dev" "$work/ember/tools"
ln -s "$src/sys/dev/ic" "$work/sys/dev/ic"
ln -s "$src/sys/dev/ic" "$work/include/dev/ic"
# The fixture includes the selected actual runtime by its normal relative path.
cp "$tools/rtw8821c-runtime-contract.c" "$work/ember/tools/"
for header in sys/param.h sys/systm.h; do
    printf '#include <errno.h>\n#include "rtw88-test-kernel.h"\n' > "$work/include/$header"
done
"$CC" -std=c11 -Wall -Wextra -Werror ${RTW88_TEST_CFLAGS:-} \
    -I"$work/include" -I"$src/ember/tools" \
    "$work/ember/tools/rtw8821c-runtime-contract.c" -o "$work/test"
if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/test"; else "$work/test"; fi
if [ "$analyze" -eq 1 ]; then
    : "${CSA:?select CSA using SDK required-role clang}"
    "$CC" -std=c11 -dM -E -x c /dev/null > "$work/predefined.h"
    cppcheck --quiet --check-level=exhaustive --enable=warning,performance,portability \
        --error-exitcode=1 --include="$work/predefined.h" --std=c11 --platform=unix64 \
        -I"$work/include" -I"$src/ember/tools" "$work/ember/tools/rtw8821c-runtime-contract.c"
    "$CSA" --analyze -std=c11 -Wall -Wextra -Werror \
        -Xanalyzer -analyzer-werror -I"$work/include" -I"$src/ember/tools" \
        "$work/ember/tools/rtw8821c-runtime-contract.c" -o "$work/runtime.plist"
fi
