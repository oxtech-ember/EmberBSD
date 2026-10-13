#!/bin/sh
# Origin: EmberBSD (AI-assisted), exercise the actual font ioctl case.
set -eu
[ "$#" = 2 ] || { echo "Usage: $0 SOURCE_ROOT NEW_WORK" >&2; exit 2; }
src=$1 work=$2
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
mkdir "$work"
awk '/^struct wsdisplay_font \{/ {copy=1} copy {print} copy && /^};/ {exit}' \
    "$src/sys/dev/wscons/wsconsio.h" > "$work/font.h"
awk '/case WSDISPLAYIO_LDFONT:/ {copy=1} copy {print} copy && /^#undef d/ {exit}' \
    "$src/sys/dev/wscons/wsdisplay.c" > "$work/font-case.h"
grep -q '^#undef d' "$work/font-case.h"
${CC:-cc} ${CFLAGS:--O2 -Wall -Wextra -Werror} -I "$work" \
    "$here/wsdisplay-font.c" -o "$work/font-test"
"$work/font-test"
