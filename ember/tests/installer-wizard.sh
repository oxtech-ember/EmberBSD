#!/bin/sh
# Origin: EmberBSD host text installer contract runner, 2026-10-13.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
${CC:-cc} -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I"$root/usr.sbin/sysinst" "$root/ember/tests/installer-wizard.c" \
    "$root/usr.sbin/sysinst/wizard_ui.c" -o "$tmp/wizard-test"
"$tmp/wizard-test"
