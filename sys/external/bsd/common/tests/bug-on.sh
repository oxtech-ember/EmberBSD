#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# EmberBSD contributors (AI-assisted).
# Run the actual compatibility header with and without kernel DIAGNOSTIC.
set -eu
[ "$#" = 1 ] && [ -x "$1" ] || { echo 'usage: bug-on.sh ABSOLUTE_HOST_CC' >&2; exit 2; }
case "$1" in /*) ;; *) exit 2;; esac
cc=$1
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/ember-bug-on.XXXXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp/include/sys" "$tmp/include/linux"
cat > "$tmp/include/sys/systm.h" <<'HEADER'
#ifndef __predict_false
#define __predict_false(x) __builtin_expect(!!(x), 0)
#endif
#ifdef DIAGNOSTIC
#define KASSERT(e) do { if (!(e)) panic("KASSERT"); } while (0)
#else
#define KASSERT(e) ((void)sizeof((long)(e)))
#endif
HEADER
: > "$tmp/include/linux/build_bug.h"
for mode in normal diagnostic; do
    if [ "$mode" = diagnostic ]; then define=-DDIAGNOSTIC; else define=-UDIAGNOSTIC; fi
    "$cc" -fuse-ld=lld -B"$(dirname "$cc")" -std=c11 -Wall -Wextra -Werror "$define" -I"$tmp/include" "$here/bug-on.c" -o "$tmp/$mode"
    "$tmp/$mode"
    echo "PASS: BUG_ON $mode"
done
