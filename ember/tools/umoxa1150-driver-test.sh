#!/bin/sh
# Origin: EmberBSD actual UPort 1150 parent contract runner, 2026-10-13.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
umask 077
[ "$#" -eq 1 ] || exit 1
case "$1" in /*) ;; *) exit 1 ;; esac
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
mkdir -p "$(dirname -- "$1")"
parent=$(CDPATH= cd -- "$(dirname -- "$1")" && pwd -P)
out=$parent/$(basename -- "$1")
case "$out/" in "$src/"*) exit 1 ;; esac
mkdir "$out"
cc=${CC:-cc}
for mode in plain sanitized; do
    flags='-O2'
    [ "$mode" = plain ] || flags='-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined'
    "$cc" -std=c99 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-parameter $flags \
        -I "$src/ember/tests/umoxa1150" -I "$src/sys/dev/usb" -idirafter "$src/sys" \
        "$src/ember/tools/umoxa1150-driver-test.c" "$src/sys/dev/usb/umoxa1150_protocol.c" \
        -o "$out/test-$mode" > "$out/build-$mode.log" 2>&1 || { cat "$out/build-$mode.log"; exit 1; }
    "$out/test-$mode" > "$out/run-$mode.log" 2>&1 || { cat "$out/run-$mode.log"; exit 1; }
    cat "$out/run-$mode.log"
done
printf 'actual USB parent with external doubles; not physical USB or native kernel execution\n' > "$out/scope.txt"
