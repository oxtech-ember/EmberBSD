#!/bin/sh
# Origin: EmberBSD - run the USB-A resource translation contract without hardware.
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
tmp=$(mktemp -d "${TMPDIR:-/tmp}/a133-usb-fdt.XXXXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
${CC:-cc} -std=c99 -Wall -Wextra -Werror ${CPPFLAGS:-} \
    "$src/ember/tools/a133-usb-fdt-test.c" \
    "$src/sys/arch/arm/sunxi/sun50i_a133_fdt.c" \
    ${LDFLAGS:-} -lfdt -o "$tmp/test"
"$tmp/test"
