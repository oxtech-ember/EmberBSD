#!/bin/sh
# Origin: EmberBSD actual G2 USB-parent contract runner, 2026-10-13.
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
"$cc" --version > "$out/compiler.txt"
find "$src/ember/tests/umoxa-g2" -type f -exec shasum -a 256 '{}' + > "$out/sources.sha256"
for f in umoxa_g2.c umoxa_g2_io.c umoxa_g2_tty.c umoxa_g2var.h umoxa_g2io.h umoxa_g2_protocol.c umoxa_g2_protocol.h umoxa_frame.c umoxa_frame.h umoxa_g2_txq.c umoxa_g2_txq.h; do
 shasum -a 256 "$src/sys/dev/usb/$f" >> "$out/sources.sha256"
done
shasum -a 256 "$src/ember/tools/umoxa-g2-driver-test.c" "$src/ember/tools/umoxa-g2-driver-test.sh" >> "$out/sources.sha256"
for mode in plain sanitized; do
 flags='-O2'
 [ "$mode" = plain ] || flags='-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined'
 "$cc" -std=c99 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-parameter $flags \
  -I "$src/ember/tests/umoxa-g2" -I "$src/sys/dev/usb" -idirafter "$src/sys" \
  "$src/ember/tools/umoxa-g2-driver-test.c" "$src/sys/dev/usb/umoxa_g2_protocol.c" \
  "$src/sys/dev/usb/umoxa_g2_txq.c" "$src/sys/dev/usb/umoxa_frame.c" -o "$out/test-$mode" \
  > "$out/build-$mode.log" 2>&1 || { cat "$out/build-$mode.log"; exit 1; }
 "$out/test-$mode" > "$out/run-$mode.log" 2>&1 || { cat "$out/run-$mode.log"; exit 1; }
 cat "$out/run-$mode.log"
done
printf 'Actual parent with external USB/kernel doubles; not native USB, concurrency or firmware/hardware acceptance.\n' > "$out/scope.txt"
