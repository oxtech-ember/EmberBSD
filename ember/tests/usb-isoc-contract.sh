#!/bin/sh
# Origin: EmberBSD (AI-assisted). Exercise the actual isochronous transfer
# functions of xhci(4) and uvideo(4) with deterministic stubs.
set -eu
[ "$#" = 3 ] || { echo "Usage: $0 XHCI_SOURCE UVIDEO_SOURCE NEW_WORK" >&2; exit 2; }
xhci=$1
uvideo=$2
work=$3
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
mkdir "$work"
extract() {
    awk -v name="$2" '
        $0 ~ "^" name "\\(" {print prev; copy=1}
        copy {print}
        copy && /^}/ {exit}
        {prev=$0}
    ' "$1" > "$work/$2.h"
}
awk '/^struct xhci_pipe \{/ {copy=1} copy {print} copy && /^};/ {exit}' \
    "$xhci" > "$work/xhci-pipe.h"
test -s "$work/xhci-pipe.h"
for name in xhci_pipe_restart_async_task xhci_device_isoc_enter; do
    extract "$xhci" "$name"
    test -s "$work/$name.h"
done
# Absent before the repair: the contract then reports the missing handling.
extract "$xhci" xhci_xfer_stalled
if [ -s "$work/xhci_xfer_stalled.h" ]; then
    echo '#define XHCI_HAVE_XFER_STALLED 1' >> "$work/xhci-pipe.h"
fi
extract "$uvideo" uvideo_stream_recv_isoc_complete
test -s "$work/uvideo_stream_recv_isoc_complete.h"
# The extracted kernel source mixes signed and unsigned operands in ways the
# kernel's own warning set accepts; -Wextra would reject it here.
for unit in xhci-isoc-contract uvideo-isoc-contract; do
    ${CC:-cc} ${CFLAGS:--O2 -g -Wall -Wextra -Werror} -Wno-sign-compare \
        -I "$work" "$here/$unit.c" -o "$work/$unit"
done
# TEST_RUNNER can wrap the checks, for example with Valgrind Memcheck.
${TEST_RUNNER:-} "$work/xhci-isoc-contract" "${USB_ISOC_TEST_CASE:-all}"
${TEST_RUNNER:-} "$work/uvideo-isoc-contract" "${USB_ISOC_TEST_CASE:-all}"
