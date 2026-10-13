#!/bin/sh
# Origin: EmberBSD (AI-assisted). Exercise the actual xHCI recovery functions.
set -eu
[ "$#" = 2 ] || { echo "Usage: $0 XHCI_SOURCE NEW_WORK" >&2; exit 2; }
source=$1
work=$2
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
mkdir "$work"
awk '/^struct xhci_pipe \{/ {copy=1} copy {print} copy && /^};/ {exit}' \
    "$source" > "$work/xhci-pipe.h"
for name in xhci_pipe_restart xhci_abortx xhci_pipe_restart_async_task \
    xhci_pipe_restart_async xhci_device_ctrl_start; do
    awk -v name="$name" '
        $0 ~ "^" name "\\(" {print prev; copy=1}
        copy {print}
        copy && /^}/ {exit}
        {prev=$0}
    ' "$source" > "$work/$name.h"
    test -s "$work/$name.h"
done
if grep -q xp_restart_cv "$work/xhci-pipe.h"; then
    echo '#define XHCI_HAVE_RESTART_CV 1' >> "$work/xhci-pipe.h"
fi
${CC:-cc} ${CFLAGS:--O2 -g -Wall -Wextra -Werror} -pthread -I "$work" \
    "$here/xhci-control-recovery.c" -o "$work/check"
"$work/check" "${XHCI_TEST_CASE:-all}"
