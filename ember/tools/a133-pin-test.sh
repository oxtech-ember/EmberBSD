#!/bin/sh
# Origin: EmberBSD - compile the actual EMAC enable routine against a register bank.
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
tmp=$(mktemp -d "${TMPDIR:-/tmp}/a133-pins.XXXXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
awk '
/^sunxi_emac_a133_enable\(/ { print "static void"; active=1 }
active { print }
active && /^}/ { exit }
' "$src/sys/arch/arm/sunxi/sunxi_emac.c" > "$tmp/a133-emac-enable.inc"
${CC:-cc} -std=c99 -Wall -Wextra -Werror -I"$tmp" \
    "$src/ember/tools/a133-pin-test.c" -o "$tmp/test"
"$tmp/test"
