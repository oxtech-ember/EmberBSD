#!/bin/sh
# Origin: EmberBSD - run the production guarded RTL8723DS read helper contract.
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
[ -f "$src/sys/dev/sdmmc/rtl8723ds_read.c" ] || { echo 'FAIL: RTL8723DS read transport is missing' >&2; exit 1; }
tmp=$(mktemp -d "${TMPDIR:-/tmp}/rtl8723ds-read.XXXXXXXX")
trap 'rm -f "$tmp/test" "$tmp/sdio-fixture.inc" "$tmp/native-match.inc"; rmdir "$tmp"' EXIT HUP INT TERM
awk '
    /^#define HOST / { active=1 }
    active && /^int$/ { exit }
    active { print }
' "$src/ember/tools/a133-sdio-fdt-test.c" > "$tmp/sdio-fixture.inc"
awk '
    /^rtl8723ds_probe_match\(/ { print "static int"; active=1 }
    active { print }
    active && /^}/ { exit }
' "$src/sys/dev/sdmmc/rtl8723ds_probe.c" > "$tmp/native-match.inc"
${CC:-cc} -std=c99 -Wall -Wextra -Werror -Wno-unused-parameter ${CPPFLAGS:-} \
    -I"$tmp" -I"$src/sys/dev/sdmmc" "$src/ember/tools/rtl8723ds-read-test.c" \
    "$src/sys/dev/sdmmc/rtl8723ds_read.c" \
    "$src/sys/arch/arm/sunxi/sun50i_a133_fdt.c" \
    ${LDFLAGS:-} -lfdt -o "$tmp/test"
"$tmp/test"
