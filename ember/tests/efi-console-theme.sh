#!/bin/sh
# Origin: EmberBSD firmware palette validation, 2026-10-13.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
"${CC:-cc}" -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined \
    "$root/ember/tests/efi-console-theme.c" "$root/usr.sbin/sysinst/wizard_ui.c" \
    -o "$tmp/theme-test"
"$tmp/theme-test"
printf '%s\n' 'PASS: UEFI colors match sysinst; RGB/BGR, reserved bits, bounds, stride and repeated rendering'
