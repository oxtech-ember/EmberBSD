#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Run on the target with an already configured wpa_supplicant and DHCP client.
# Usage: wifi-roam-check.sh interface NEW-log-directory [seconds [DNS-name]]
# Exit: 0 PASS, 1 FAIL, 2 usage/setup error, 77 SKIP (no suitable alternate AP).

set -u
umask 077
LC_ALL=C
PATH=${PATH:-/bin:/usr/bin}:/sbin:/usr/sbin
export LC_ALL PATH

if [ "$#" -lt 2 ] || [ "$#" -gt 4 ]; then
    echo "usage: $0 interface NEW-log-directory [seconds [DNS-name]]" >&2
    exit 2
fi
interface=$1
logs=$2
limit=${3:-75}
dns_name=${4:-example.com}
case "$interface" in ''|*[!a-zA-Z0-9_.:-]*) exit 2 ;; esac
case "$limit" in ''|0*|*[!0-9]*) exit 2 ;; esac
[ "$limit" -le 300 ] 2>/dev/null || exit 2
mkdir "$logs" || exit 2
start=$(date +%s) || exit 2
deadline=$((start + limit))

finish()
{
    printf 'result=%s\nreason=%s\n' "$1" "$2" > "$logs/result"
    printf 'WIFI_ROAM_CHECK result=%s reason=%s\n' "$1" "$2"
    case "$1" in PASS) exit 0 ;; SKIP) exit 77 ;; *) exit 1 ;; esac
}
trap 'finish FAIL interrupted' HUP INT TERM

before()
{
    now=$(date +%s) || finish FAIL clock
    [ "$now" -lt "$1" ] && [ "$now" -lt "$deadline" ]
}

ctl()
{
    output=$1
    shift
    before "$deadline" || finish FAIL deadline
    wpa_cli -i "$interface" "$@" > "$logs/$output" 2>&1 || return 1
    # wpa_cli can exit zero when the control reply is FAIL.
    ! grep -Eq '^(FAIL|UNKNOWN COMMAND)' "$logs/$output"
}

status()
{
    state= bssid= ssid= key_mgmt= address=
    ctl status status || return 1
    while IFS='=' read -r key value; do
        case "$key" in
        wpa_state) state=$value ;;
        bssid) bssid=$value ;;
        ssid) ssid=$value ;;
        key_mgmt) key_mgmt=$value ;;
        ip_address) address=$value ;;
        esac
    done < "$logs/status"
}

printf 'WIFI_ROAM_CHECK phase=baseline\n'
baseline_end=$((start + 25))
while before "$baseline_end"; do
    if status && [ "$state" = COMPLETED ] && [ -n "$bssid" ] &&
        [ -n "$ssid" ] && [ -n "$address" ] && [ "$address" != 0.0.0.0 ]; then
        break
    fi
    sleep 1
done
[ "${state:-}" = COMPLETED ] && [ -n "${bssid:-}" ] &&
    [ -n "${ssid:-}" ] && [ -n "${address:-}" ] && [ "$address" != 0.0.0.0 ] ||
    finish FAIL baseline_not_ready
cp "$logs/status" "$logs/baseline" || finish FAIL log_write
network_ssid=$ssid
network_key=$key_mgmt
case "$network_key" in
SAE) security=SAE ;;
WPA2-PSK) security=PSK ;;
*) finish SKIP unsupported_profile ;;
esac

# Flush only unused cache entries. wpa_bss_flush preserves in-use BSSes;
# neither the saved networks nor the current association are reconfigured.
ctl flush bss_flush 0 || finish FAIL cache_flush
grep -qx OK "$logs/flush" || finish FAIL cache_flush
ctl baseline-cache scan_results || finish FAIL cache_read
scan_started=$(date +%s) || finish FAIL clock
ctl scan scan || finish FAIL scan_request
grep -qx OK "$logs/scan" || finish FAIL scan_request
printf 'WIFI_ROAM_CHECK phase=fresh_candidate\n'
now=$(date +%s) || finish FAIL clock
candidate_end=$((now + 25))
candidate=
tab=$(printf '\t')
while before "$candidate_end"; do
    if status && [ "$state" = COMPLETED ] &&
        [ "$ssid" = "$network_ssid" ] && [ "$key_mgmt" = "$network_key" ]; then
        ctl scan-results scan_results || finish FAIL cache_read
        while IFS="$tab" read -r ap freq signal flags name; do
            before "$candidate_end" || break
            case "$ap" in
            ??:??:??:??:??:??) ;;
            *) continue ;;
            esac
            [ "$ap" != "$bssid" ] && [ "$name" = "$network_ssid" ] || continue
            case "$security:$flags" in SAE:*SAE*|PSK:*PSK*) ;; *) continue ;; esac
            ctl candidate bss "$ap" || finish FAIL bss_control
            record_bssid= record_ssid= record_age=
            while IFS='=' read -r key value; do
                case "$key" in
                bssid) record_bssid=$value ;;
                ssid) record_ssid=$value ;;
                age) record_age=$value ;;
                esac
            done < "$logs/candidate"
            [ "$record_bssid" = "$ap" ] &&
                [ "$record_ssid" = "$network_ssid" ] || continue
            if grep -q "^$ap$tab" "$logs/baseline-cache"; then
                # Scan may reconnect to the other AP. Accept the former
                # current BSS only after its preserved record is refreshed.
                case "$record_age" in ''|*[!0-9]*) continue ;; esac
                now=$(date +%s) || finish FAIL clock
                # BSS age has whole-second resolution.
                [ "$record_age" -le "$((now - scan_started + 1))" ] || continue
            fi
            candidate=$ap
            break
        done < "$logs/scan-results"
        if [ -n "$candidate" ]; then
            status || finish FAIL status_before_roam
            [ "$ssid" = "$network_ssid" ] && [ "$key_mgmt" = "$network_key" ] ||
                finish FAIL profile_changed
            if [ "$state" = COMPLETED ] && [ "$bssid" != "$candidate" ]; then
                break
            fi
            # Selection itself may have coincided with an automatic move.
            candidate=
        fi
    fi
    sleep 1
done
before "$deadline" || finish FAIL deadline
if [ -z "$candidate" ]; then
    [ "${state:-}" = COMPLETED ] || finish FAIL scan_connection_not_ready
    finish SKIP no_fresh_compatible_candidate
fi
cp "$logs/status" "$logs/before-roam" || finish FAIL log_write

# The cache/status checks and single ROAM request run locally without a host gap.
# A successful control reply is distinct from a completed association.
printf 'WIFI_ROAM_CHECK phase=roam\n'
printf '%s\n' "$candidate" > "$logs/requested-bssid"
ctl roam roam "$candidate" || finish FAIL roam_control
grep -qx OK "$logs/roam" || finish FAIL roam_control
while before "$deadline"; do
    if status && [ "$state" = COMPLETED ] && [ "$bssid" = "$candidate" ] &&
        [ "$ssid" = "$network_ssid" ] && [ "$key_mgmt" = "$network_key" ] &&
        [ -n "$address" ]; then
        break
    fi
    sleep 1
done
[ "${state:-}" = COMPLETED ] && [ "${bssid:-}" = "$candidate" ] &&
    [ "${ssid:-}" = "$network_ssid" ] && [ "${key_mgmt:-}" = "$network_key" ] &&
    [ -n "${address:-}" ] || finish FAIL roam_not_completed
cp "$logs/status" "$logs/after-roam" || finish FAIL log_write
printf 'WIFI_ROAM_CHECK phase=connectivity\n'
before "$deadline" || finish FAIL deadline
RES_OPTIONS='attempts:1 timeout:2' getent hosts "$dns_name" > "$logs/dns" 2>&1 ||
    finish FAIL dns
before "$deadline" || finish FAIL deadline
route -n get default > "$logs/route" 2>&1 || finish FAIL default_route
gateway=
while read -r key value rest; do
    [ "$key" != gateway: ] || gateway=$value
done < "$logs/route"
[ -n "$gateway" ] || finish FAIL default_route
before "$deadline" || finish FAIL deadline
ping -n -c 1 -w 3 "$gateway" > "$logs/ping" 2>&1 || finish FAIL gateway_ping
finish PASS roam_and_connectivity
