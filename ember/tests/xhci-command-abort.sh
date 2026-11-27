#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted).
# Exercise the production xHCI command abort and command ring restart on a
# development host.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
usb=$here/../../sys/dev/usb
work=$(mktemp -d "${TMPDIR:-/tmp}/xhci-command-abort.XXXXXX")
trap 'rm -rf "$work"' EXIT
awk '/enum \{/ {buf=""; keep=1} keep {buf=buf $0 "\n"}
    keep && /\} xr_cmd_state;/ {printf "%s", buf; exit}' \
    "$usb/xhcivar.h" > "$work/xhci-cmd-state.inc"
grep -q XHCI_CR_BLOCKED "$work/xhci-cmd-state.inc"
for name in xhci_host_dequeue xhci_event_cmd xhci_abort_command \
    xhci_do_command_locked; do
	awk -v name="$name" '
	    $0 ~ "^" name "\\(" {print prev; copy=1}
	    copy {print}
	    copy && /^}/ {exit}
	    {prev=$0}
	' "$usb/xhci.c" >> "$work/xhci-command.inc"
	grep -q "^$name(" "$work/xhci-command.inc"
done
${CC:-cc} ${CFLAGS:--O1 -g} -std=gnu11 -Wall -Werror -Wno-unused-variable \
    -Wno-unused-function -UNDEBUG -I "$work" "$here/xhci-command-abort.c" \
    -o "$work/check"
"$work/check"
