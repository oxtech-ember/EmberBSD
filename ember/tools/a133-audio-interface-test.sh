#!/bin/sh
# Origin: EmberBSD - run production adapter guards with finite hardware fixtures.
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
driver="$src/sys/arch/arm/sunxi/sun50i_a133_codec.c"
[ -f "$driver" ] || { echo 'FAIL: A133 audio interface is missing' >&2; exit 1; }
tmp=$(mktemp -d "${TMPDIR:-/tmp}/a133-audio-interface.XXXXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
for fn in a133_params a133_set_format a133_trigger a133_halt a133_service a133_interrupt; do
    kind=int
    [ "$fn" != a133_service ] || kind=void
    awk -v name="$fn" -v kind="$kind" '
    $0 ~ "^" name "\\(" { print "static " kind; active=1 }
    active { print }
    active && /^}/ { exit }
    ' "$driver" >> "$tmp/adapter.inc"
done
${CC:-cc} -std=c99 -Wall -Wextra -Werror -I"$tmp" \
    -I"$src/sys/arch/arm/sunxi" "$src/ember/tools/a133-audio-interface-test.c" \
    "$src/sys/arch/arm/sunxi/sun50i_a133_codec_io.c" \
    "$src/sys/arch/arm/sunxi/sun50i_a133_pcm.c" -o "$tmp/test"
"$tmp/test"
