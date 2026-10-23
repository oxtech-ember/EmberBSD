#!/bin/sh
# Execute actual scan storage, cleanup and bounded copyout consumers.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
[ "$#" -eq 0 ] || exit 2
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TEST_OBJDIR:-${TMPDIR:-/tmp}}/net80211-phy-ies.XXXXXXXX")
if [ "${PHY_IES_TEST_KEEP:-0}" != 1 ]; then
    trap 'rm -rf "$work"' EXIT HUP INT TERM
else
    printf 'Test sources: %s\n' "$work"
fi
awk '/^#define[[:space:]]+IEEE80211_(ADDR_LEN|NWID_LEN)[[:space:]]/ {print}' \
    "$src/sys/net80211/ieee80211.h" > "$work/scan-abi.h"
awk '/^#define[[:space:]]+IEEE80211_NODE_(QOS|WME_OFFER|WME_VALID|ASSOC_PENDING|AMSDU_RX)[[:space:]]/ {print}' \
    "$src/sys/net80211/ieee80211_node.h" >> "$work/scan-abi.h"
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
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
extra=
if [ -f "$src/sys/net80211/ieee80211_phy_ies.c" ]; then
    mkdir -p "$work/sys" "$work/net80211"
    printf '#include <string.h>\n#include <stdint.h>\n' > "$work/sys/systm.h"
    ln -s "$src/sys/net80211/ieee80211_phy_ies.h" "$work/net80211/ieee80211_phy_ies.h"
    ln -s "$src/sys/net80211/ieee80211_beacon_ies.h" "$work/net80211/ieee80211_beacon_ies.h"
    extra="$src/sys/net80211/ieee80211_phy_ies.c $src/sys/net80211/ieee80211_beacon_ies.c"
fi
python3 "$src/ember/tools/net80211-phy-ies-extract.py" "$src" "$work"
${CC:-gcc-16} -std=c11 -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -Wall -Werror ${PHY_IES_TEST_CFLAGS:-} \
    -DBEACON_IES_TEST -I"$work" -I"$src/sys/net80211" "$src/ember/tools/net80211-phy-ies-contract.c" $extra -o "$work/check"
if [ -n "${TEST_RUNNER:-}" ]; then
    "$TEST_RUNNER" "$work/check"
else
    "$work/check"
fi
