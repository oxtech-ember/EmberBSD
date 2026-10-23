#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
tests=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
hid=$(dirname -- "$tests")
sys=$(CDPATH= cd -- "$hid/../.." && pwd)
cc=${CC:-cc}
case $("$cc" -dumpversion) in
16|16.*) ;;
*) echo "Use GCC 16 for these target tests." >&2; exit 1 ;;
esac
build=${BUILD_DIR:-$(mktemp -d)}
mkdir -p "$build"
if [ -z "${BUILD_DIR:-}" ]; then
    trap 'rm -f "$build/hid.o" "$build/t_hidmt"; rmdir "$build"' EXIT HUP INT TERM
fi
# The existing HID parser retains its native warning policy.
"$cc" -std=c23 ${CFLAGS:-} -I"$tests/include" -I"$sys" \
    -c "$hid/hid.c" -o "$build/hid.o"
"$cc" -std=c23 -Wall -Wextra -Werror ${CFLAGS:-} \
    -I"$tests/include" -I"$sys" "$build/hid.o" "$hid/hidmt.c" \
    "$tests/t_hidmt.c" -o "$build/t_hidmt"
"$build/t_hidmt"
