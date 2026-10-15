#!/bin/sh
# Origin: EmberBSD - test the production Goodix coordinate decoder.
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
tmp=$(mktemp -d "${TMPDIR:-/tmp}/gt9xx-frames.XXXXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
${CC:-cc} -std=c99 -Wall -Wextra -Werror -I"$src/sys/dev/i2c" \
    "$src/ember/tools/gt9xx-frame-test.c" "$src/sys/dev/i2c/gt9xx_frame.c" \
    -o "$tmp/test"
"$tmp/test"
