#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: unchanged pinned FreeBSD MAC power setup trace generator, AI-assisted.
# Usage: rtw8822c-power-trace-gen.sh REFDIR > rtw8822c-power.trace
set -eu
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
[ "$#" -eq 1 ] || exit 2
ref=$1
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/rtw8822c-power-gen.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
check() {
    sum=$(shasum -a 256 "$ref/$1" | cut -d' ' -f1)
    [ "$sum" = "$2" ] || { echo "$1: incorrect SHA256" >&2; exit 1; }
}
check mac.c ec8a96ef305231f15af22bc94ad5708e0daf3bfb3ff470ec96757913e2be59f6
check reg.h c8246f9305539347fdb62b0294fe12ea24769cb3ad56c2b0235c4269a0307324
check rtw8822c.c 6986e6107da9261b97277d57fcc11daf5dcb0d71571c55b1080bdea1495ce160
python3 "$tools/rtw88-ref-extract.py" "$ref" "$work/defs.h" reg.h:defines
ruby -e 's=File.read(ARGV[0]); m=s.match(/\.sys_func_en\s*=\s*(0x[0-9a-fA-F]+),/); abort "missing sys_func_en" unless m; puts "#define REF_SYS_FUNC_EN #{m[1]}"' "$ref/rtw8822c.c" >> "$work/defs.h"
python3 "$tools/rtw88-ref-extract.py" "$ref" "$work/body.c" \
    mac.c:fn:rtw_mac_pre_system_cfg mac.c:fn:__rtw_mac_init_system_cfg
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${RTW88_TEST_CFLAGS:-} \
    -I"$work" -I"$tools" "$tools/rtw8822c-power-trace-ref.c" -o "$work/gen"
"$work/gen"
