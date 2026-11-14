#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/net80211-sta.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
${CC:-cc} -std=c99 -Wall -Wextra -Werror ${STA_TEST_CFLAGS:-${CFLAGS:-}} \
    "$src/sys/net80211/tests/sta.c" -o "$work/sta"
if [ -n "${TEST_RUNNER:-}" ]; then
    "$TEST_RUNNER" "$work/sta"
else
    "$work/sta"
fi

# Exercise the production connected-scan lifetime helpers with minimal net80211
# fixtures, keeping cache epoch and routing-event ownership in the common layer.
cat "$src/sys/net80211/tests/sta-scan.c" > "$work/sta-scan.c"
for spec in ieee80211_sta_scan_begin:int ieee80211_sta_scan_match:bool \
    ieee80211_sta_scan_report:bool ieee80211_sta_scan_release:bool \
    ieee80211_notify_sta_scan:void; do
    fn=${spec%%:*}; type=${spec#*:}
    awk -v fn="$fn" -v type="$type" '
        $0 ~ "^" fn "\\(" {copy=1; print "static " type}
        copy {print} copy && /^}/ {exit}' \
    "$src/sys/net80211/ieee80211_netbsd.c" >> "$work/sta-scan.c"
done
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${STA_TEST_CFLAGS:-${CFLAGS:-}} \
    "$work/sta-scan.c" -o "$work/sta-scan"
if [ -n "${TEST_RUNNER:-}" ]; then
    "$TEST_RUNNER" "$work/sta-scan"
else
    "$work/sta-scan"
fi
