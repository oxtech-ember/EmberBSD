#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
# Check byacc attributes with the selected native GCC or Clang compiler.
set -eu
repo=${1:?usage: byacc-attributes.sh SOURCE_ROOT ABSOLUTE_CC}
cc=${2:?usage: byacc-attributes.sh SOURCE_ROOT ABSOLUTE_CC}
case "$cc" in /*) ;; *) echo "compiler must be an absolute path" >&2; exit 2;; esac
if grep -Eq -- "-DGCC_(NORETURN|PRINTFLIKE|SCANFLIKE|UNUSED)" "$repo/external/bsd/byacc/bin/Makefile"; then
    echo "Makefile overrides configured attributes" >&2
    exit 1
fi
config=$repo/external/bsd/byacc/include/config.h
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
"$cc" --version
cp "$config" "$work/config.h"
cat > "$work/probe.c" <<'SRC'
#include "config.h"
GCC_NORETURN void die(void);
void print_msg(const char *, ...) GCC_PRINTFLIKE(1, 2);
void scan_msg(const char *, ...) GCC_SCANFLIKE(1, 2);
static void unused_function(void) GCC_UNUSED;
static void unused_function(void) {}
void valid(void) { print_msg("%d", 1); scan_msg("%d", (int *)0); }
#ifdef BAD_PRINTF
void bad_printf(void) { print_msg("%d", "wrong"); }
#endif
#ifdef BAD_SCANF
void bad_scanf(void) { scan_msg("%d", (char *)0); }
#endif
SRC
compile() { "$cc" -std=c11 -Wall -Wextra -Werror -Wformat -fsyntax-only -I "$work" "$@" "$work/probe.c"; }
compile
for bad in BAD_PRINTF BAD_SCANF; do
  if compile "-D$bad" > "$work/$bad.log" 2>&1; then echo "$bad: unexpectedly accepted"; exit 1; fi
  if ! grep -qi 'format' "$work/$bad.log"; then cat "$work/$bad.log"; exit 1; fi
  echo "$bad: rejected as expected"
done
# Reconstruct the four original definitions only in the temporary copy.
sed -e 's/^#define GCC_NORETURN .*/#define GCC_NORETURN __dead/' \
  -e 's/^#define GCC_PRINTFLIKE(a, b) .*/#define GCC_PRINTFLIKE(a, b) __printflike(a, b)/' \
  -e 's/^#define GCC_SCANFLIKE(a, b) .*/#define GCC_SCANFLIKE(a, b) __scanflike(a, b)/' \
  -e 's/^#define GCC_UNUSED .*/#define GCC_UNUSED __unused/' "$work/config.h" > "$work/original.h"
mv "$work/original.h" "$work/config.h"
if compile > "$work/baseline.log" 2>&1; then echo 'baseline unexpectedly accepted'; exit 1; fi
if ! grep -Eq '__dead|__printflike|__scanflike|__unused' "$work/baseline.log"; then cat "$work/baseline.log"; exit 1; fi
echo 'baseline rejected as expected'
echo 'attribute probe: PASS'
