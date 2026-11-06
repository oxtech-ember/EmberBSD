#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Native PHY bounded checks; CC must name the selected host compiler.
set -eu
: "${CC:?Select the host SDK C compiler in CC}"
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$tools/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/rtw8821c-phy.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work/dev/ic" "$work/sys"
cp "$tools/rtw8821c-test-var.h" "$work/dev/ic/rtw88var.h"
printf '#include <stddef.h>\n' > "$work/sys/null.h"
printf '#include_next <sys/errno.h>\n' > "$work/sys/errno.h"
printf '#include <string.h>\n' > "$work/sys/systm.h"
"$CC" -std=c11 -Wall -Wextra -Werror ${RTW88_TEST_CFLAGS:-} -D_KERNEL -include stdint.h -I "$work" -I "$root/sys" -I "$root/sys/dev/ic" "$tools/rtw8821c-native-smoke.c" "$root/sys/dev/ic/rtw8821c_phy.c" "$root/sys/dev/ic/rtw8821c_table.c" "$root/sys/dev/ic/rtw88_phy_table.c" -o "$work/native"
"$work/native"
"$CC" -std=c11 -Wall -Wextra -Werror ${RTW88_TEST_CFLAGS:-} -I "$root/sys" -I "$root/sys/dev/ic" "$tools/rtw8821c-table-contract.c" "$root/sys/dev/ic/rtw8821c_phy.c" "$root/sys/dev/ic/rtw8821c_table.c" "$root/sys/dev/ic/rtw88_phy_table.c" -o "$work/tables"
"$work/tables"
"$CC" -std=c11 -Wall -Wextra -Werror ${RTW88_TEST_CFLAGS:-} -I "$root/sys" -I "$root/sys/dev/ic" "$tools/rtw8821c-txpower-contract.c" "$root/sys/dev/ic/rtw8821c_txpwr.c" "$root/sys/dev/ic/rtw8821c_table.c" -o "$work/power"
"$work/power"
