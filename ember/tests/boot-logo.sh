#!/bin/sh
# Origin: EmberBSD host and standalone boot logo checks, 2026-10-13.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
"${CC:-cc}" -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined \
    "$root/ember/tests/boot-logo.c" -o "$tmp/logo-test"
"$tmp/logo-test"
if [ "$#" -eq 1 ]; then
    ln -s "$root/sys/arch/amd64/include" "$tmp/machine"
    ln -s "$root/sys/arch/x86/include" "$tmp/x86"
    "$1" -E -nostdinc -D_STANDALONE -I"$root/sys" -I"$tmp" \
        "$root/sys/arch/i386/stand/efiboot/ember_logo.h" -o "$tmp/standalone.i"
elif [ "$#" -ne 0 ]; then
    echo "Usage: sh ember/tests/boot-logo.sh [amd64-cross-gcc]" >&2
    exit 2
fi
echo 'PASS: logo geometry, palette and requested standalone includes'
