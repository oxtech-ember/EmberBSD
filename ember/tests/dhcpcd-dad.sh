#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted). Exercise production DHCP DAD transitions.
# Usage: dhcpcd-dad.sh SOURCE_ROOT NEW_WORK [DHCP_SOURCE]
# DHCP_SOURCE selects an older dhcp.c for the negative control.
# Set CC/CFLAGS for cross compilation and DHCPCD_DAD_RUN=0 to only build.
# The host fixture substitutes OS/I/O calls. It is not a wire DHCP test.
set -eu
[ "$#" -ge 2 ] && [ "$#" -le 3 ] || {
	printf 'usage: %s SOURCE_ROOT NEW_WORK [DHCP_SOURCE]\n' "$0" >&2
	exit 2
}
root=$1
work=$2
source=${3:-$root/external/bsd/dhcpcd/dist/src/dhcp.c}
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
headers=$root/external/bsd/dhcpcd/dist/src
[ -f "$source" ] && [ -f "$headers/dhcp.h" ]
mkdir "$work"

# Retain upstream authorship and copy complete function bodies unchanged.
awk '/^#include/ { exit } { print }' "$source" > "$work/production.h"
for name in dhcp_discover dhcp_finish_dad dhcp_arp_address dhcp_arp_bind \
    dhcp_lastlease dhcp_handleifa; do
	awk -v name="$name" '
	    $0 ~ "^" name "[(]" { print previous; copying = 1 }
	    copying { print }
	    copying && /^}/ { found = 1; exit }
	    { previous = $0 }
	    END { if (!found) exit 1 }
	' "$source" >> "$work/production.h"
done
awk '/^enum DHS/ { copying = 1 }
    copying { print }
    copying && /^};/ { found = 1; exit }
    END { if (!found) exit 1 }' "$headers/dhcp.h" > "$work/states.h"
awk '/^#define DHCPCD_(STATIC|INFORM|IPV4LL|TEST|CONFIGURE|BOOTP|REQUEST|MANAGER)[[:space:]]/ { print }' \
    "$headers/if-options.h" > "$work/options.h"
awk '/^#define STATE_(EXPIRED|ADDED)[[:space:]]/ { print }' \
    "$headers/ipv4.h" >> "$work/options.h"
printf 'DHCP source: %s\n' "$source"
if command -v sha256 >/dev/null 2>&1; then
	sha256 "$source"
else
	shasum -a 256 "$source"
fi
${CC:-cc} ${CFLAGS:--O2 -g -std=c11 -Wall -Wextra -Werror} \
    -I"$work" "$here/dhcpcd-dad.c" -o "$work/check"
if [ "${DHCPCD_DAD_RUN:-1}" = 1 ]; then
	"$work/check"
else
	printf 'Built target test: %s/check (not run)\n' "$work"
fi
