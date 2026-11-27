#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted).
# Exercise the production hub routine that addresses a root hub device once
# more after xhci(4) restarted its command ring, on a development host.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
usb=$here/../../sys/dev/usb
work=$(mktemp -d "${TMPDIR:-/tmp}/usb-hub-address-retry.XXXXXX")
trap 'rm -rf "$work"' EXIT
for name in uhub_retry_attached uhub_retry_later uhub_retry_address; do
	awk -v name="$name" '
	    $0 ~ "^" name "\\(" {print prev; copy=1}
	    copy {print}
	    copy && /^}/ {exit}
	    {prev=$0}
	' "$usb/uhub.c" >> "$work/uhub_retry.inc"
	grep -q "^$name(" "$work/uhub_retry.inc"
done
grep -q USBD_REENUMERATE "$usb/usbdi.h"
${CC:-cc} ${CFLAGS:--O2 -g} -std=gnu11 -Wall -Wextra -Werror -UNDEBUG \
    -I "$work" -I "$usb" "$here/usb-hub-address-retry.c" -o "$work/check"
"$work/check"
