#!/bin/sh
# Origin: EmberBSD - test the actual board reset sequence against GPIO banks.
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
tmp=$(mktemp -d "${TMPDIR:-/tmp}/gt9xx-reset.XXXXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
awk '
/^gt9xx_ys_m33_reset\(/ { print "static int"; active=1 }
active { print }
active && /^}/ { exit }
' "$src/sys/dev/i2c/gt9xx_ys_m33.c" > "$tmp/gt9xx-reset.inc"
${CC:-cc} -std=c99 -Wall -Wextra -Werror -I"$tmp" \
    "$src/ember/tools/gt9xx-reset-test.c" -o "$tmp/test"
"$tmp/test"
