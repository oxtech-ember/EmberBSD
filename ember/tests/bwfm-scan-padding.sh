#!/bin/sh
# Execute the production scan serializer with a poisoned scratch buffer.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/bwfm-scan-padding.XXXXXXXX")
if [ "${BWFM_TEST_KEEP:-0}" != 1 ]; then
    trap 'rm -rf "$work"' EXIT HUP INT TERM
else
    printf 'Test sources: %s\n' "$work"
fi
awk '/^#define[[:space:]]+IEEE80211_(ADDR_LEN|NWID_LEN)[[:space:]]/ {print}' \
    "$src/sys/net80211/ieee80211.h" > "$work/scan-abi.h"
awk '/^#define[[:space:]]+IEEE80211_RATE_MAXSIZE[[:space:]]/ {print}' \
    "$src/sys/net80211/_ieee80211.h" >> "$work/scan-abi.h"
awk '/^#define[[:space:]]+IEEE80211_SCAN_(CAPINFO_HI|RSSI_DBM)[[:space:]]/ {print}
    /^struct ieee80211req_scan_result / {copy=1}
    copy {print} copy && /^};/ {exit}' \
    "$src/sys/net80211/ieee80211_ioctl.h" >> "$work/scan-abi.h"
printf 'static void\n' > "$work/get_scan_result.h"
awk '/^get_scan_result\(/ {copy=1}
    copy {print} copy && /^}/ {exit}' \
    "$src/sys/net80211/ieee80211_ioctl.c" >> "$work/get_scan_result.h"
printf 'static int\n' > "$work/getscanresults.h"
awk '/^ieee80211_ioctl_getscanresults\(/ {copy=1}
    copy && /^\tp = ireq->i_data;/ {
        print "\t/* Test setup: initialize the entire automatic scratch buffer. */"
        print "\tmemset(&u, 0xa5, sizeof(u));"
    }
    copy {print} copy && /^}/ {exit}' \
    "$src/sys/net80211/ieee80211_ioctl.c" >> "$work/getscanresults.h"
cp "$src/ember/tools/bwfm-scan-padding.c" "$work/check.c"
${CC:-cc} -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Werror ${CFLAGS:-} \
    -I"$work" "$work/check.c" -o "$work/check"
"$work/check"
