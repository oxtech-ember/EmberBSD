#!/bin/sh
# Exercise the driver against the Lyra WLAN endpoint layout and bounded replies.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/uaicfw.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
# The USB core passes usbif_attach_arg only to interface attachments.
awk '$1 == "attach" && $2 == "uaicfw" {
        total++; if ($3 == "at" && $4 == "usbifif") iface++
    } END { exit !(total == 1 && iface == 1) }' "$src/sys/dev/usb/files.usb" || {
    echo "uaicfw must match a USB interface" >&2
    exit 1
}
sed -n '/^#define[[:space:]]*UAICFW_IDENT_REG/,$p' \
    "$src/sys/dev/usb/uaicfw.c" > "$work/probe.h"
awk '/^aicwf_usb_queue_limit\(/ {copy=1; print "static unsigned"}
copy {print} copy && /^}/ {exit}' "$src/sys/dev/usb/if_aicwf_usb.c" > "$work/survey-rx.h"
awk '/^aicwf_usb_port\(/ {copy=1; print "static uint64_t"}
copy {print} copy && /^}/ {exit}' "$src/sys/dev/usb/if_aicwf_usb.c" > "$work/station-write.h"
awk '/^aicwf_usb_write\(/ {copy=1; print "static int"}
copy {print} copy && /^}/ {exit}' "$src/sys/dev/usb/if_aicwf_usb.c" >> "$work/station-write.h"
${CC:-cc} -std=c99 -Wall -Wextra -Werror -Wno-unused-parameter ${AICWF_USB_CFLAGS:-} \
    -I"$src/sys/dev/ic" -I"$src/sys/dev/usb" -I"$work" "$src/ember/tools/aicwf-usb-id.c" \
    -o "$work/check"
"$work/check"
