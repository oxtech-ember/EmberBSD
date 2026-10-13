#!/bin/sh
# Origin: EmberBSD (AI-assisted). Test actual USB abort and ioctl code.
set -eu
[ "$#" = 3 ] || { echo "Usage: $0 USBDI_SOURCE UGEN_SOURCE NEW_WORK" >&2; exit 2; }
usbdi=$1
ugen=$2
work=$3
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
mkdir "$work"
for name in usbd_abort_default_request usbd_abort_default_pipe usbd_abort_pipe \
    usbd_suspend_pipe usbd_resume_pipe usbd_ar_pipe usbd_xfer_abort \
    usbd_start_next; do
    awk -v name="$name" '
        $0 ~ "^" name "\\(" {print prev; copy=1}
        copy {print}
        copy && /^}/ {exit}
        {prev=$0}
    ' "$usbdi" > "$work/$name.h"
    # Older source does not provide the new helper. Its ioctl calls the old one.
    test "$name" = usbd_abort_default_request || test -s "$work/$name.h"
done
awk '/case USB_ABORT_REQUEST:/ {copy=1}
    copy && /case USB_DO_REQUEST:/ {exit}
    copy {print}' "$ugen" > "$work/abort-case.h"
test -s "$work/abort-case.h"
awk '/case USB_DO_REQUEST:/ {request=1}
    request && /if.*flag & FWRITE/ {copy=1}
    copy && /Avoid requests/ {exit}
    copy {print}' "$ugen" > "$work/request-flags.h"
test -s "$work/request-flags.h"
${CC:-cc} ${CFLAGS:--O2 -g -std=c11 -Wall -Wextra -Werror} -pthread \
    -I "$work" "$here/usb-abort-request.c" -o "$work/check"
"$work/check" "${USB_TEST_CASE:-all}"
