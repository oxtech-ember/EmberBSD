#!/bin/sh
# Exercise the driver against the Lyra WLAN endpoint layout and bounded replies.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/uaicfw.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
sed -n '/^#define[[:space:]]*UAICFW_IDENT_REG/,$p' \
    "$src/sys/dev/usb/uaicfw.c" > "$work/probe.h"
${CC:-cc} -std=c99 -Wall -Wextra -Werror -Wno-unused-parameter \
    -I"$src/sys/dev/ic" -I"$src/sys/dev/usb" -I"$work" "$src/ember/tools/aicwf-usb-id.c" \
    -o "$work/check"
"$work/check"
