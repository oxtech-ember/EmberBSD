#!/bin/sh
# Origin: EmberBSD G2 control/firmware contract runner, 2026-10-13.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
umask 077
[ "$#" -eq 1 ] || exit 1
case "$1" in /*) ;; *) exit 1 ;; esac
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
case "$1/" in "$src/"*) exit 1 ;; esac
mkdir -p "$(dirname -- "$1")"
mkdir "$1"
out=$(CDPATH= cd -- "$1" && pwd -P)
cc=${CC:-cc}
"$cc" --version > "$out/compiler.txt"
shasum -a 256 "$src/sys/dev/usb/umoxa_g2_protocol.c" "$src/sys/dev/usb/umoxa_g2_protocol.h" "$src/ember/tools/umoxa-g2-protocol-test.c" > "$out/sources.sha256"
for mode in plain sanitized; do
 flags='-O2'
 [ "$mode" = plain ] || flags='-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined'
 "$cc" -std=c99 -Wall -Wextra -Werror $flags -I "$src/sys/dev/usb" \
  "$src/ember/tools/umoxa-g2-protocol-test.c" "$src/sys/dev/usb/umoxa_g2_protocol.c" \
  -o "$out/test-$mode" > "$out/build-$mode.log" 2>&1 || { cat "$out/build-$mode.log"; exit 1; }
 "$out/test-$mode" > "$out/run-$mode.log" 2>&1 || { cat "$out/run-$mode.log"; exit 1; }
 cat "$out/run-$mode.log"
done
printf 'Protocol and synthetic I/O only; no USB, firmware asset or hardware acceptance.\n' > "$out/scope.txt"
