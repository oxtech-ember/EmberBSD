#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Review Dev Bots #472 on the real imt.c + hid.c + hidmt.c with userland
# doubles (any C23 compiler, no GCC 16 needed).
#   CC=clang sh run-imt-gesture.sh
#   LEGACY_SYS=<sys/ of the pre-review tree> sh run-imt-gesture.sh   (red run)
# Extra: CFLAGS (sanitizers), BUILD_DIR (keep the build).
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
mine=$(CDPATH= cd -- "$here/../../.." && pwd)
src=$mine
legacy=
if [ -n "${LEGACY_SYS:-}" ]; then src=$LEGACY_SYS; legacy=-DLEGACY; fi
cc=${CC:-cc}
build=${BUILD_DIR:-$(mktemp -d)}
mkdir -p "$build"
[ -n "${BUILD_DIR:-}" ] || trap 'rm -rf "$build"' EXIT HUP INT TERM
hidtests=$mine/dev/hid/tests
# idirafter: kernel headers not in the stripped tree come from this tree.
set -- "-I$hidtests/include" -idirafter "$src" -idirafter "$mine"
grep -v '^#include' "$src/dev/i2c/imt.c" > "$build/adapter.c"
"$cc" -std=c23 ${CFLAGS:-} "$@" -c "$src/dev/hid/hid.c" -o "$build/hid.o" 2>/dev/null
"$cc" -std=c23 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function \
    ${CFLAGS:-} $legacy "$@" -I"$build" -I"$here" -I"$hidtests" \
    "$build/hid.o" "$src/dev/hid/hidmt.c" "$here/t_imt_gesture.c" \
    -o "$build/t_imt_gesture"
"$build/t_imt_gesture"
# The stub-based contract test of the adapter (suspend/resume, mode report).
if [ -z "$legacy" ]; then
    "$cc" -std=c23 -Wall -Wextra -Werror -Wno-unused-parameter \
        -Wno-unused-function ${CFLAGS:-} "$@" -I"$build" -I"$here" \
        "$here/t_imt.c" -o "$build/t_imt"
    "$build/t_imt"
fi
