#!/bin/sh
# Exercise the current driver functions with bounded firmware and queue fixtures.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/bwfm-sta.XXXXXXXX")
if [ "${BWFM_TEST_KEEP:-0}" != 1 ]; then
    trap 'rm -rf "$work"' EXIT HUP INT TERM
else
    printf 'Test sources: %s\n' "$work"
fi
for fn in bwfm_sta_security bwfm_sta_wnm bwfm_sta_ready bwfm_sta_tx_done \
    bwfm_sta_scan_cancel bwfm_sta_mgmt bwfm_sta_mgmt_cb bwfm_sta_assoc_ies \
    bwfm_sta_scan_done bwfm_fwvar_var_get_checked; do
    awk -v fn="$fn" '$0 ~ "^" fn "\\(" {copy=1}
        copy {print} copy && /^}/ {exit}' "$src/sys/dev/ic/bwfm.c" > "$work/$fn.h"
    test -s "$work/$fn.h"
done
awk '/^#define[[:space:]]+IEEE80211_STA_/ {print}
    /^struct ieee80211req_sta_mgmt/ {copy=1}
    copy {print} copy && /^};/ {exit}' \
    "$src/sys/net80211/ieee80211_ioctl.h" > "$work/abi.h"
test -s "$work/abi.h"
awk '/^#define[[:space:]]+IEEE80211_F_(WPA|WPA1|WPA2|SCAN)[[:space:]]/ {print}' \
    "$src/sys/net80211/ieee80211_var.h" > "$work/flags.h"
cp "$src/sys/net80211/ieee80211_rsn.h" "$src/sys/dev/ic/bwfm_sta.h" "$work/"
cp "$src/ember/tools/bwfm-sta-contract.c" "$work/check.c"
${CC:-cc} -std=c99 -Wall -Wextra -Werror -Wno-unused-parameter ${CFLAGS:-} \
    -I"$work" "$work/check.c" -o "$work/check"
"$work/check"
for fn in bsd_sta_scan_clear bsd_sta_scan_timeout bsd_sta_abort_scan bsd_sta_tx_status \
    bsd_sta_disconnected bsd_sta_tx_timeout bsd_sta_tx_body bsd_sta_rx_body \
    bsd_sta_event; do
    awk -v fn="$fn" '$0 ~ "^" fn "\\(" {copy=1}
        copy {print} copy && /^}/ {exit}' \
        "$src/external/bsd/wpa/dist/src/drivers/driver_bsd.c" > "$work/$fn.h"
    test -s "$work/$fn.h"
done
awk '/^#define[[:space:]]+RTM_IEEE80211_STA_(RX|TX|SCAN)[[:space:]]/ {print}' \
    "$src/sys/net80211/ieee80211_netbsd.h" > "$work/routes.h"
awk '/^[[:space:]]*\.abort_scan[[:space:]]*=/ {print}' \
    "$src/external/bsd/wpa/dist/src/drivers/driver_bsd.c" > "$work/bsd-scan-ops.h"
test -s "$work/bsd-scan-ops.h"
cp "$src/ember/tools/bwfm-sta-bsd.c" "$work/bsd-check.c"
${CC:-cc} -std=c99 -Wall -Wextra -Werror -Wno-unused-parameter ${CFLAGS:-} \
    -I"$work" "$work/bsd-check.c" -o "$work/bsd-check"
"$work/bsd-check"
awk '/result->caps = sr->isr_capinfo;/ {copy=1}
    copy && /pos = \(u8 \*\)\(result \+ 1\);/ {exit}
    copy {print}' "$src/external/bsd/wpa/dist/src/drivers/driver_bsd.c" \
    > "$work/bsd-scan-metadata.h"
awk '/^#define[[:space:]]+IEEE80211_SCAN_(CAPINFO_HI|RSSI_DBM)[[:space:]]/ {print}' \
    "$src/sys/net80211/ieee80211_ioctl.h" > "$work/scan-flags.h"
awk '/^#define[[:space:]]+WPA_SCAN_(LEVEL_DBM|NOISE_INVALID)[[:space:]]/ {print}' \
    "$src/external/bsd/wpa/dist/src/drivers/driver.h" >> "$work/scan-flags.h"
test -s "$work/bsd-scan-metadata.h"
cp "$src/ember/tools/bwfm-sta-scan.c" "$work/scan-check.c"
${CC:-cc} -std=c99 -Wall -Wextra -Werror ${CFLAGS:-} \
    -I"$work" "$work/scan-check.c" -o "$work/scan-check"
"$work/scan-check"

# Use the upstream driver parameter types and the production association and
# scan callbacks. The fixture replaces only the ioctl, timer and event edges.
for fn in bsd_sta_associate wpa_driver_bsd_associate wpa_driver_bsd_scan; do
    awk -v fn="$fn" '$0 ~ "^" fn "\\(" {copy=1}
        copy {print} copy && /^}/ {exit}' \
        "$src/external/bsd/wpa/dist/src/drivers/driver_bsd.c" > "$work/$fn.h"
    test -s "$work/$fn.h"
done
awk '/case RTM_IEEE80211_ASSOC:/ {copy=1}
    copy && /case RTM_IEEE80211_DISASSOC:/ {exit}
    copy {print}' "$src/external/bsd/wpa/dist/src/drivers/driver_bsd.c" \
    > "$work/bsd-assoc-event.h"
test -s "$work/bsd-assoc-event.h"
awk '/^#define[[:space:]]+IEEE80211_(STA|SAE|IOC_(STA_MGMT|SAE|MLME|PRIVACY|WPA|ROAMING|SCAN_REQ))[[:space:]_]/ {print}
    /^struct ieee80211req_(sta_mgmt|mlme) / {copy=1}
    copy {print} copy && /^};/ {copy=0}' \
    "$src/sys/net80211/ieee80211_ioctl.h" > "$work/assoc-abi.h"
awk '/^#define[[:space:]]+RTM_IEEE80211_(ASSOC|REASSOC)[[:space:]]/ {print}' \
    "$src/sys/net80211/ieee80211_netbsd.h" > "$work/assoc-routes.h"
cp "$src/ember/tools/bwfm-sta-bsd-assoc.c" "$work/assoc-check.c"
${CC:-cc} -std=c99 -Wall -Wextra -Werror -Wno-unused-parameter ${CFLAGS:-} \
    -D_DEFAULT_SOURCE -I"$src/external/bsd/wpa/dist/src" -I"$work" \
    "$work/assoc-check.c" -o "$work/assoc-check"
"$work/assoc-check"
