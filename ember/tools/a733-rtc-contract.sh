#!/bin/sh
# Origin: EmberBSD; execute the production RTC and hardware DCXO query.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=${RTC_SOURCE_ROOT:-$(CDPATH= cd -- "$tools/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/a733-rtc.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
sed '/^#include /d; /^__KERNEL_RCSID(/d' \
    "$src/sys/arch/arm/sunxi/sunxi_rtc.c" > "$work/rtc.h"
cp "$src/sys/arch/arm/sunxi/sunxi_rtcvar.h" "$work/"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
    ${RTC_TEST_CFLAGS:-} -I"$work" "$tools/a733-rtc-contract.c" -o "$work/test"
"$work/test"
