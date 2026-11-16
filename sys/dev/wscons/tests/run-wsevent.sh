#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Host test of the free-slot rule of the wscons event queue (wsevent.c).
# LEGACY=1 builds the previous rule (red run).
set -eu
tests=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=${1:-$tests/../wsevent.c}
cc=${CC:-cc}
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT HUP INT TERM
awk '
    /wsevent-room begin/ { on = 1; next }
    /wsevent-room end/   { on = 0; next }
    on { print }
' "$src" > "$out/wsevent-room-extract.h"
extra=
if [ -n "${LEGACY:-}" ]; then
    extra=-DLEGACY
elif [ ! -s "$out/wsevent-room-extract.h" ]; then
    echo "no wsevent-room code in $src" >&2
    exit 1
fi
"$cc" -std=c11 -Wall -Wextra -Werror $extra -I"$out" \
    "$tests/t_wsevent_room.c" -o "$out/t"
"$out/t"
