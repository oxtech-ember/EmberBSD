#!/bin/sh
# Origin: EmberBSD - exercise the checked SDIO function lifecycle.
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
[ -f "$src/sys/dev/sdmmc/rtl8723ds_function.c" ] || {
    echo 'FAIL: checked RTL8723DS function lifecycle is missing' >&2
    exit 1
}
tmp=$(mktemp -d "${TMPDIR:-/tmp}/rtl8723ds-function.XXXXXXXX")
trap 'rm -rf -- "$tmp"' EXIT HUP INT TERM
${CC:-cc} -std=c99 -Wall -Wextra -Werror ${CPPFLAGS:-} \
    -I"$src/sys/dev/sdmmc" "$src/ember/tools/rtl8723ds-function-test.c" \
    "$src/sys/dev/sdmmc/rtl8723ds_function.c" ${LDFLAGS:-} -o "$tmp/test"
"$tmp/test"
