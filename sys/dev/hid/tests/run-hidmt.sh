#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
tests=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
hid=$(dirname -- "$tests")
sys=$(CDPATH= cd -- "$hid/../.." && pwd)
cc=${CC:-cc}
build=${BUILD_DIR:-$(mktemp -d)}
mkdir -p "$build"
if [ -z "${BUILD_DIR:-}" ]; then
    trap 'rm -f "$build/hid.o" "$build/t_hidmt"; rmdir "$build"' EXIT HUP INT TERM
fi
# The existing HID parser retains its native warning policy.
"$cc" -std=c23 ${CFLAGS:-} -I"$tests/include" -idirafter "$sys" \
    -c "$hid/hid.c" -o "$build/hid.o"
"$cc" -std=c23 -Wall -Wextra -Werror ${CFLAGS:-} \
    -I"$tests/include" -idirafter "$sys" "$build/hid.o" "$hid/hidmt.c" \
    "$tests/t_hidmt.c" -o "$build/t_hidmt"
"$build/t_hidmt"
