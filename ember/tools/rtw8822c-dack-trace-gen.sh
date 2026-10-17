#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: unchanged pinned cold ADCK/DACK trace consumer, AI-assisted.
# Usage: rtw8822c-dack-trace-gen.sh REFDIR > rtw8822c-dack.trace
set -eu
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
[ "$#" -eq 1 ] || exit 2
ref=$1
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/rtw8822c-dack-gen.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
check() {
    sum=$(shasum -a 256 "$ref/$1" | cut -d' ' -f1)
    [ "$sum" = "$2" ] || { echo "$1: incorrect SHA256" >&2; exit 1; }
}
check rtw8822c.c 6986e6107da9261b97277d57fcc11daf5dcb0d71571c55b1080bdea1495ce160
check rtw8822c.h 975cb88c627ebabb93729c6b5082a6274a9fe69a11094830db83e95486e93b18
check main.h bab159780b164959b5bd981d24544507558ed816334d49fd1a31d4d17a69285e
check util.c f5b17292be0028da0b5296caa4bb062f879a8b92161ccc299e092bb64d39a8a8
check phy.c 669d76c8acfc9f30418dbd119e777eae4404f6e3bb1198292aab536a3aa9e4ec
python3 "$tools/rtw88-ref-extract.py" "$ref" "$work/defs.h" \
    rtw8822c.h:defines main.h:type:rtw_backup_info
printf '#define INV_RF_DATA 0xffffffff\n#define __ffs(x) __builtin_ctz(x)\n' >> "$work/defs.h"
ruby - "$tools" "$ref" "$work" <<'RUBY'
tools,ref,work=ARGV
text=File.readlines(File.join(ref,'rtw8822c.c'))[107...1008].join
names=text.scan(/^static[^\n]*?\b((?:rtw|__rtw)\w+)\(/).flatten
abort 'incorrect function boundary' unless names.first=='rtw8822c_dac_backup_reg' && names.last=='rtw8822c_rf_dac_cal'
args=['python3',File.join(tools,'rtw88-ref-extract.py'),ref,File.join(work,'body.c'),
      'phy.c:fn:rtw_phy_read_rf','phy.c:fn:rtw_phy_write_rf_reg_sipi',
      'phy.c:fn:rtw_phy_write_rf_reg','phy.c:fn:rtw_phy_write_rf_reg_mix',
      'util.c:fn:check_hw_ready','util.c:fn:rtw_restore_reg']
args.concat(names.map { |n| "rtw8822c.c:fn:#{n}" })
abort 'extract failed' unless system(*args)
RUBY
${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter \
    ${RTW88_TEST_CFLAGS:-} -I"$work" -I"$tools" \
    "$tools/rtw8822c-dack-trace-ref.c" -o "$work/gen"
"$work/gen"
