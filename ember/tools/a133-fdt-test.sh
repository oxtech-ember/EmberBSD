#!/bin/sh
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
adapter="$src/sys/arch/arm/sunxi/sun50i_a133_fdt.c"
if [ ! -f "$adapter" ]; then
    echo "FAIL: vendor A133 MMC node has no resource adapter" >&2
    exit 1
fi
tmp=$(mktemp -d "${TMPDIR:-/tmp}/a133-fdt.XXXXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
${CC:-cc} -std=c99 -Wall -Wextra -Werror ${CPPFLAGS:-} \
    "$src/ember/tools/a133-fdt-test.c" "$adapter" \
    ${LDFLAGS:-} -lfdt -o "$tmp/test"
"$tmp/test"
