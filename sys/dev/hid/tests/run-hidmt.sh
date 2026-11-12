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
    trap 'rm -f "$build/hid.o" "$build/baseline.o" "$build/t_hidmt" "$build/t_hidmt_hybrid" "$build/t_hidmt_gesture"; rmdir "$build"' EXIT HUP INT TERM
fi
# The existing HID parser retains its native warning policy.
"$cc" -std=c23 ${CFLAGS:-} -I"$tests/include" -idirafter "$sys" \
    -c "$hid/hid.c" -o "$build/hid.o"
"$cc" -std=c23 -Wall -Wextra -Werror ${CFLAGS:-} \
    -I"$tests/include" -idirafter "$sys" "$build/hid.o" "$hid/hidmt.c" \
    "$tests/t_hidmt.c" -o "$build/t_hidmt"
"$build/t_hidmt"
# Frozen pre-hybrid hidmt.c with renamed symbols: golden reference.
"$cc" -std=c23 -Wall -Wextra -Werror ${CFLAGS:-} \
    -Dhidmt_parse=old_hidmt_parse -Dhidmt_decode=old_hidmt_decode \
    -Dhidmt_pointer_update=old_hidmt_pointer_update \
    -I"$tests/include" -idirafter "$sys" -c "$tests/hidmt_baseline.c" \
    -o "$build/baseline.o"
"$cc" -std=c23 -Wall -Wextra -Werror ${CFLAGS:-} \
    -I"$tests/include" -idirafter "$sys" "$build/hid.o" "$build/baseline.o" \
    "$hid/hidmt.c" "$tests/t_hidmt_hybrid.c" -o "$build/t_hidmt_hybrid"
"$build/t_hidmt_hybrid"
# Three/four-finger gestures on the real ELAN descriptor.
"$cc" -std=c23 -Wall -Wextra -Werror ${CFLAGS:-} \
    -I"$tests/include" -idirafter "$sys" "$build/hid.o" "$hid/hidmt.c" \
    "$tests/t_hidmt_gesture.c" -o "$build/t_hidmt_gesture"
"$build/t_hidmt_gesture"
