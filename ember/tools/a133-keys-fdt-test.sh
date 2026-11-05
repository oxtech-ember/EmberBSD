#!/bin/sh
# Origin: EmberBSD - run the A133 keys contract with the selected SDK host compiler.
set -eu
: "${CC:?select the SDK host compiler}"
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=${1:-$(CDPATH= cd -- "$tools/../.." && pwd)}
tmp=$(mktemp -d "${TMPDIR:-/tmp}/a133-keys.XXXXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
sunxi="$src/sys/arch/arm/sunxi"
awk '$2=="LRADC_BGR_REG" || /SUNXI_CCU_RESET\(A100_RST_BUS_LRADC,/ || /SUNXI_CCU_GATE\(A100_CLK_BUS_LRADC,/' \
    "$sunxi/sun50i_a100_ccu.c" > "$tmp/keys-ccu.h"
sed -n '/^sunxi_lradc_volume_intr(/,/^}/p' "$sunxi/sunxi_lradc.c" |
    { printf 'static int\n'; cat; } > "$tmp/keys-intr.h"
"$CC" -std=c99 -Wall -Wextra -Werror ${CPPFLAGS:-} -I"$src/sys/arch" -I"$tmp" \
    "$tools/a133-keys-fdt-test.c" "$sunxi/sun50i_a133_fdt.c" ${LDFLAGS:-} -lfdt -o "$tmp/test"
"$tmp/test"
