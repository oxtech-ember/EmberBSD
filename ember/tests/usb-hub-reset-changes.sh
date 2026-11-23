#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted).
# Exercise the production hub routine that clears port changes after a
# failed reset on a development host.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
usb=$here/../../sys/dev/usb
work=$(mktemp -d "${TMPDIR:-/tmp}/usb-hub-reset-changes.XXXXXX")
trap 'rm -rf "$work"' EXIT
awk '
    /^uhub_drop_reset_changes\(/ {print prev; copy=1}
    copy {print}
    copy && /^}/ {exit}
    {prev=$0}
' "$usb/uhub.c" > "$work/uhub_drop_reset_changes.inc"
test -s "$work/uhub_drop_reset_changes.inc"
${CC:-cc} ${CFLAGS:--O2 -g} -std=gnu11 -Wall -Wextra -Werror -UNDEBUG \
    -I "$work" -I "$usb" "$here/usb-hub-reset-changes.c" -o "$work/check"
"$work/check"
