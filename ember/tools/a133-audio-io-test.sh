#!/bin/sh
# Origin: EmberBSD - test codec lifecycle and FIFO transfers without hardware.
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
base="$src/sys/arch/arm/sunxi"
[ -f "$base/sun50i_a133_codec_io.c" ] || {
    echo 'FAIL: production A133 codec I/O helper is missing' >&2; exit 1;
}
tmp=$(mktemp -d "${TMPDIR:-/tmp}/a133-audio-io.XXXXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
${CC:-cc} -std=c99 -Wall -Wextra -Werror -I"$base" \
    "$src/ember/tools/a133-audio-io-test.c" \
    "$base/sun50i_a133_codec_io.c" "$base/sun50i_a133_pcm.c" -o "$tmp/test"
"$tmp/test"
