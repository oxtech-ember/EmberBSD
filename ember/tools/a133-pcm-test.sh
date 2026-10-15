#!/bin/sh
# Origin: EmberBSD - test the production A133 PCM transfer core without audio I/O.
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
core="$src/sys/arch/arm/sunxi/sun50i_a133_pcm.c"
if [ ! -f "$core" ]; then
    echo 'FAIL: production A133 PCM core is missing' >&2
    exit 1
fi
tmp=$(mktemp -d "${TMPDIR:-/tmp}/a133-pcm.XXXXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
${CC:-cc} -std=c99 -Wall -Wextra -Werror ${CPPFLAGS:-} \
    -I"$src/sys/arch/arm/sunxi" "$src/ember/tools/a133-pcm-test.c" \
    "$core" ${LDFLAGS:-} -o "$tmp/test"
"$tmp/test"
