#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu

src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
dwc2="$src/sys/external/bsd/dwc2/dist"
work=$(mktemp -d "${TMPDIR:-/tmp}/dwc2-reset.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

awk '/^static void dwc2_port_intr\(/ { copy=1 }
    copy { print }
    copy && /^}/ { exit }' "$dwc2/dwc2_hcdintr.c" > "$work/port-intr.inc"
test -s "$work/port-intr.inc"
${CC:-cc} ${CFLAGS:--O2} -std=c11 -Wall -Wextra -Werror -I "$work" -I "$dwc2" \
    "$src/ember/tests/dwc2-port-reset.c" -o "$work/check"
"$work/check"
