#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted). Host checks of the actual probe and USB header.
set -eu
[ "$#" = 2 ] || { echo "usage: $0 source-root new-output-directory" >&2; exit 1; }
src=$(cd "$1" && pwd)
out=$2
[ ! -e "$out" ] || { echo 'output directory already exists' >&2; exit 1; }
mkdir -p "$out/include/dev/usb"
ln -s "$src/sys/dev/usb/usb.h" "$out/include/dev/usb/usb.h"
${CC:-gcc-16} ${CFLAGS:--O1 -g -Wall -Wextra -Werror} -std=c11 \
    -I"$out/include" "$src/ember/tests/usb-hub-status.c" -o "$out/check"
"$out/check"
