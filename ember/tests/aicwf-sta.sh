#!/bin/sh
# Check the real AIC management path and the BSD station client contract.
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; keep the extracted RX contract aligned with the driver ABI.
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
wpa=${EMBER_WPA_SOURCE:?Set EMBER_WPA_SOURCE to the prepared Ports WPA source}
driver=${AICWF_STA_SOURCE:-$src/sys/dev/ic/aicwf.c}
defs=${AICWF_STA_DEFS:-$wpa/src/common/ieee802_11_defs.h}
work=$(mktemp -d "${TEST_OBJDIR:-${TMPDIR:-/tmp}}/aicwf-sta.XXXXXXXX")
if [ "${AICWF_STA_KEEP:-0}" = 1 ]; then
    printf 'Test sources: %s\n' "$work"
else
    trap 'rm -rf "$work"' EXIT HUP INT TERM
fi
awk '/^#define[[:space:]]+IEEE80211_(STA|SAE)_/ {print}
    /^#define[[:space:]]+IEEE80211_IOC_(WPA|MLME|SCAN_REQ|CHANLIST)[[:space:]]/ {print}
    /^struct ieee80211req_(sta_mgmt|sae) / {copy=1}
    copy {print} copy && /^};/ {copy=0}' \
    "$src/sys/net80211/ieee80211_ioctl.h" > "$work/abi.h"
(printf '#ifndef AICWF_TEST_IEEE80211_ABI_H\n#define AICWF_TEST_IEEE80211_ABI_H\n';
    cat "$work/abi.h"; printf '\n#endif\n') > "$work/abi.tmp"
mv "$work/abi.tmp" "$work/abi.h"
awk '/^#define[[:space:]]+RTM_IEEE80211_(SAE|STA_(RX|TX|SCAN))[[:space:]]/ {print}' \
    "$src/sys/net80211/ieee80211_netbsd.h" > "$work/routes.h"
awk '/^#define WLAN_ACTION_(WNM|RADIO_MEASUREMENT) / {print}
    /^#define WLAN_RRM_/ {print}
    /WNM_BSS_TRANS_MGMT_(QUERY|REQ|RESP) =/ {
        gsub(/[,=]/, ""); print "#define " $1 " " $2
    }' "$defs" \
    > "$work/actions.h"
awk '/^#define[[:space:]]+AICWF_(IF_STA|MM_(ADD|REMOVE)_IF_CFM|RXHDR_|RX_FLAG_UPLOAD|RX_DECR[[:space:]]|KEY_SLOT_PAIRWISE|HWKEY_NONE|SM_(CONNECT|DISCONNECT)|CONNECT_|AUTH_SAE|BAND_)/ {print}' \
    "$driver" > "$work/aicwf-defs.h"
cp "$src/sys/dev/ic/aicwf_command.h" "$src/sys/dev/ic/aicwf_sae.h" "$src/sys/dev/ic/aicwf_vht.h" "$src/sys/dev/ic/aicwf_sta.h" "$src/sys/net80211/ieee80211_rsn.h" "$src/sys/net80211/ieee80211_sta.h" "$work/"
mkdir -p "$work/net80211" "$work/sys"
cp "$src/sys/net80211/ieee80211_sta_action.h" "$work/net80211/"
cp "$work/abi.h" "$work/net80211/ieee80211_ioctl.h"
cp "$work/ieee80211_sta.h" "$work/net80211/"
printf '#include_next <sys/errno.h>\n' > "$work/sys/errno.h"
sed '/^#include/,$d' "$src/sys/net80211/ieee80211_ioctl.c" > "$work/driver.h"
awk '/^ieee80211_sta_request_validate\(/ {copy=1; print "static int"}
    copy {print} copy && /^}/ {exit}' \
    "$src/sys/net80211/ieee80211_ioctl.c" >> "$work/driver.h"
awk '/^aicwf_sdio_write\(/ { copy=1; print "static int" }
    copy {print} copy && /^}/ {exit}' "$src/sys/dev/sdmmc/if_aicwf.c" >> "$work/driver.h"
for spec in aicwf_sta_action_valid:bool aicwf_sta_supported:bool \
    aicwf_sta_security:bool aicwf_sta_ready:bool aicwf_write_attempted:int \
    aicwf_write:int aicwf_survey_add_reply:int aicwf_cmd_reply:int aicwf_cmd:int aicwf_fw_cmd:int aicwf_confirm:void \
    aicwf_sta_tx_finish_locked:bool aicwf_sta_scan_finish_locked:bool \
    aicwf_sta_scan_cancel_locked:void aicwf_sta_retire_locked:void aicwf_sae_advance_if:uint64_t \
    aicwf_sae_advance:void aicwf_tx_power:int8_t aicwf_dc_channel:bool \
    aicwf_survey_remove_vif:int aicwf_survey_scan_status:int aicwf_survey_set:int aicwf_chan_set:void aicwf_chan_list:u_int aicwf_add_vif:int aicwf_passive_scan:void aicwf_scan:void aicwf_connect:void \
    aicwf_sae_clear_keys:void aicwf_disconnect:void aicwf_newstate_cb:void aicwf_state_cb:void \
    aicwf_sta_tx_cfm:void aicwf_sta_scan_request:int aicwf_sta_mgmt:int \
    aicwf_sta_mgmt_cb:void aicwf_sta_cancel_scan_cb:void aicwf_scan_done:void \
    aicwf_sta_assoc:void aicwf_rx:void aicwf_newstate:int \
    aicwf_recover_disconnect:uint64_t aicwf_recover_scan:void aicwf_alive_work:void aicwf_recover_scan_cb:void aicwf_disconnect_ind:void \
    'aicwf_mgmt_body:const uint8_t *' aicwf_sta_rx_event:void \
    aicwf_pmf_rx:void aicwf_mgmt_rx:void aicwf_task:void; do
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
    (void)ifp; (void)&seq; (void)m;
    (void)qos; (void)amsdu; (void)tid; (void)&hdr; (void)&current; (void)serial;
}
C
awk '/^#define[[:space:]]+WPA_DRIVER_FLAGS_(SUPPORT_RRM|RRM_NEIGHBOR_REPORT(_ONLY)?)[[:space:]]/ {print}' \
    "$wpa/src/drivers/driver.h" >> "$work/driver.h"
: > "$work/client.h"
for spec in bsd_sta_init:void bsd_sta_tx_body:int bsd_sta_rx_body:int; do
    fn=${spec%%:*}; type=${spec#*:}
    awk -v fn="$fn" -v type="$type" '
        $0 ~ "^" fn "\\(" {copy=1; print "static " type}
        copy {print} copy && /^}/ {exit}' \
        "$wpa/src/drivers/driver_bsd.c" >> "$work/client.h"
done
cp "$src/ember/tools/aicwf-sta-contract.c" "$work/check.c"
mkdir -p "$work/sys" "$work/net80211"
ln -s "$src/sys/net80211/ieee80211_vht_ies.h" "$work/net80211/ieee80211_vht_ies.h"
ln -s "$src/sys/net80211/ieee80211_he_ies.h" "$work/net80211/ieee80211_he_ies.h"
printf '#include <stdbool.h>\n#include <stdint.h>\n#include <string.h>\n' > "$work/sys/systm.h"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
    ${AICWF_STA_CFLAGS:-} -include stdbool.h -include stdint.h -I"$work" \
    "$work/check.c" "$src/sys/net80211/ieee80211_vht_ies.c" -o "$work/check"
if [ -n "${TEST_RUNNER:-}" ]; then
    "$TEST_RUNNER" "$work/check" "${AICWF_STA_CASE:-all}"
else
    "$work/check" "${AICWF_STA_CASE:-all}"
fi
