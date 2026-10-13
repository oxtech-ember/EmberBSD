#!/bin/sh
# Check the real AIC management path and the BSD station client contract.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
driver=${AICWF_STA_SOURCE:-$src/sys/dev/sdmmc/if_aicwf.c}
defs=${AICWF_STA_DEFS:-$src/external/bsd/wpa/dist/src/common/ieee802_11_defs.h}
work=$(mktemp -d "${TMPDIR:-/tmp}/aicwf-sta.XXXXXXXX")
if [ "${AICWF_STA_KEEP:-0}" = 1 ]; then
    printf 'Test sources: %s\n' "$work"
else
    trap 'rm -rf "$work"' EXIT HUP INT TERM
fi
awk '/^#define[[:space:]]+IEEE80211_(STA|SAE)_/ {print}
    /^struct ieee80211req_(sta_mgmt|sae) / {copy=1}
    copy {print} copy && /^};/ {copy=0}' \
    "$src/sys/net80211/ieee80211_ioctl.h" > "$work/abi.h"
awk '/^#define[[:space:]]+RTM_IEEE80211_(SAE|STA_(RX|TX|SCAN))[[:space:]]/ {print}' \
    "$src/sys/net80211/ieee80211_netbsd.h" > "$work/routes.h"
awk '/^#define WLAN_ACTION_(WNM|RADIO_MEASUREMENT) / {print}
    /^#define WLAN_RRM_/ {print}
    /WNM_BSS_TRANS_MGMT_(QUERY|REQ|RESP) =/ {
        gsub(/[,=]/, ""); print "#define " $1 " " $2
    }' "$defs" \
    > "$work/actions.h"
awk '/^#define[[:space:]]+AICWF_(RXHDR_|RX_FLAG_UPLOAD|RX_DECR[[:space:]]|KEY_SLOT_PAIRWISE|HWKEY_NONE)/ {print}' \
    "$driver" > "$work/aicwf-defs.h"
cp "$src/sys/dev/sdmmc/if_aicwf_sae.h" "$src/sys/dev/sdmmc/if_aicwf_vht.h" "$src/sys/dev/sdmmc/if_aicwf_sta.h" "$src/sys/dev/ic/fullmac_sae.h" "$work/"
: > "$work/driver.h"
for spec in aicwf_sta_action_valid:bool aicwf_sta_supported:bool \
    aicwf_sta_security:bool aicwf_sta_ready:bool aicwf_write_attempted:int \
    aicwf_write:int aicwf_cmd_reply:int aicwf_cmd:int aicwf_scan:void \
    aicwf_sta_tx_finish_locked:bool aicwf_sta_scan_finish_locked:bool \
    aicwf_sta_scan_cancel_locked:void aicwf_sta_retire_locked:void aicwf_sae_advance_if:uint64_t \
    aicwf_sae_advance:void \
    aicwf_sta_tx_cfm:void aicwf_sta_scan_request:int aicwf_sta_mgmt:int \
    aicwf_sta_mgmt_cb:void aicwf_sta_cancel_scan_cb:void aicwf_scan_done:void \
    aicwf_sta_assoc:void aicwf_rx:void aicwf_newstate:int \
    aicwf_recover_disconnect:uint64_t aicwf_recover_scan:void aicwf_alive_work:void aicwf_recover_scan_cb:void aicwf_disconnect_ind:void \
    'aicwf_mgmt_body:const uint8_t *' aicwf_sta_rx_event:void \
    aicwf_pmf_rx:void aicwf_mgmt_rx:void; do
    fn=${spec%%:*}; type=${spec#*:}
    awk -v fn="$fn" -v type="$type" '
        $0 ~ "^" fn "\\(" {copy=1; print "static " type}
        copy {print} copy && /^}/ {exit}' "$driver" >> "$work/driver.h"
done
# Stop at the data-frame path. This retains the complete production management
# dispatcher, including the call order that formerly bypassed PMF admission.
awk '/^aicwf_rx_data\(/ {copy=1; print "static void"}
    copy && /Beacons and data frames without a body/ {exit}
    copy {print}' "$driver" >> "$work/driver.h"
cat >> "$work/driver.h" <<'C'
    (void)ifp; (void)&seq; (void)m; (void)tailp;
    (void)qos; (void)amsdu; (void)tid; (void)&hdr;
}
C
: > "$work/client.h"
for spec in bsd_sta_init:void bsd_sta_tx_body:int bsd_sta_rx_body:int; do
    fn=${spec%%:*}; type=${spec#*:}
    awk -v fn="$fn" -v type="$type" '
        $0 ~ "^" fn "\\(" {copy=1; print "static " type}
        copy {print} copy && /^}/ {exit}' \
        "$src/external/bsd/wpa/dist/src/drivers/driver_bsd.c" >> "$work/client.h"
done
cp "$src/ember/tools/aicwf-sta-contract.c" "$work/check.c"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
    ${AICWF_STA_CFLAGS:-} -I"$work" "$work/check.c" -o "$work/check"
"$work/check" "${AICWF_STA_CASE:-all}"
