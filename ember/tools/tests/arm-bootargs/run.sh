#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; regression for exact ARM boot option names.
set -eu
: "${CC:?Select the native GCC16 compiler}"
dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$dir/../../../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/arm-bootargs.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
awk '/^#define[[:space:]]+BOOTOPT_/ {print}' \
    "$root/sys/arch/arm/include/bootconfig.h" > "$work/options.h"
awk '/^int __noasan/ {take=1} take && /^char \*/ {exit} take {print}' \
    "$root/sys/arch/arm/arm/bootconfig.c" > "$work/function.inc"
"$CC" -std=c11 -O2 -Wall -Wextra -Werror ${CFLAGS:-} -I"$work" \
    "$dir/test.c" -o "$work/test"
"$work/test"
