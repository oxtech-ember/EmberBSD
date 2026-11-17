#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Exercise the production USB hub and serial routines on a development host.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
usb=$here/../../sys/dev/usb
work=$(mktemp -d "${TMPDIR:-/tmp}/usb-serial-hotplug.XXXXXX")
trap 'rm -rf "$work"' EXIT
extract() {
    awk -v name="$2" '
        $0 ~ "^" name "\\(" {print prev; copy=1}
        copy {print}
        copy && /^}/ {exit}
        {prev=$0}
    ' "$usb/$1.c" > "$work/$2.inc"
    test -s "$work/$2.inc"
}
extract uhub uhub_begin_explore
extract uhub uhub_intr
extract umodem umodem_match
extract umodem_common umodem_get_status
extract ucom ucom_free_xfers
extract ucom ucom_close_pipes
extract usbdi usbd_open_pipe_intr
${CC:-cc} ${CFLAGS:--O2 -g} -std=gnu11 -Wall -Wextra -Werror \
    -Wno-unused-parameter -UNDEBUG -I "$work" -I "$usb" "$here/usb-serial-hotplug.c" \
    -o "$work/check"
"$work/check"
