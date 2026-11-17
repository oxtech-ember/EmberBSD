#!/bin/sh
# Origin: EmberBSD USB Realtek packet contracts (AI-assisted).
set -eu
[ "$#" = 2 ] || { echo "Usage: $0 SOURCE NEW_WORK" >&2; exit 2; }
src=$1
work=$2
mkdir "$work"
${CC:?select the SDK C compiler} ${CFLAGS:--O2 -g -std=c11 -Wall -Wextra -Werror} \
    -I"$src/sys/dev/usb" "$src/ember/tests/urtsx-protocol.c" \
    "$src/sys/dev/usb/urtsx_proto.c" -o "$work/check"
"$work/check"
