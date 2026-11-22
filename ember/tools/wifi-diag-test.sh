#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Run the real service in an isolated copy with command/rc mocks; no network I/O.
set -eu
here=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
service="$here/../etc/rc.d/ember_wifi_diag"
work=$(mktemp -d "${TMPDIR:-/tmp}/wifi-diag-test.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work/bin" "$work/boot"
export DIAG_TEST_WORK="$work"
cat > "$work/rc.subr" <<'EOF'
load_rc_config() { :; }
warn() { echo "$*" >&2; }
run_rc_command() { [ "${ember_wifi_diag:-NO}" != YES ] || ember_wifi_diag_start; }
EOF
cat > "$work/bin/mock" <<'EOF'
#!/bin/sh
name=${0##*/}
printf '%s %s\n' "$name" "$*" >> "$DIAG_TEST_WORK/calls"
case "$name" in
timeout)
    # Verify the real command limits and supervisor deadline, then run instantly.
    case "$1" in
    -f) [ "$2:$3" = '-k:1' ] || exit 99
        case "$4" in 3|11) ;; *) exit 99;; esac
        shift 4;;
    -k) [ "$2:$3" = '5:180' ] || exit 99; shift 3;;
    *) exit 99;;
    esac
    "$@";;
mount) [ ! -e "$DIAG_TEST_WORK/unmounted" ] && echo '/dev/dk0 on /boot type msdos (local)' ;;
date) echo 100;;
sleep|sync) :;;
shutdown) [ "$*" = '-p now' ] && touch "$DIAG_TEST_WORK/powered-off";;
dmesg) echo 'fixture command timeout'; exit 124;;
wpa_cli) case "$*" in '-i aicwf0 status'|'-i aicwf0 scan_results') echo fixture;; *) exit 99;; esac;;
ps) [ "$*" = '-axo pid,ppid,stat,comm' ] || exit 99; echo fixture;;
ifconfig|netstat|tail) echo fixture;;
*) exit 99;;
esac
EOF
chmod 755 "$work/bin/mock"
for command in timeout mount date sleep sync shutdown dmesg wpa_cli ps ifconfig netstat tail; do
    ln -s mock "$work/bin/$command"
done
sed -e "s|/etc/rc.subr|$work/rc.subr|g" \
    -e "s|/etc/rc.d/ember_wifi_diag|$work/service|g" \
    -e "s|/boot/ember-diag|$work/boot/ember-diag|g" \
    -e "s|/usr/bin/timeout|$work/bin/timeout|g" \
    -e "s|/sbin/mount|$work/bin/mount|g" \
    -e "s|/bin/date|$work/bin/date|g" \
    -e "s|/bin/sleep|$work/bin/sleep|g" \
    -e "s|/bin/sync|$work/bin/sync|g" \
    -e "s|/sbin/shutdown|$work/bin/shutdown|g" \
    -e "s|/sbin/dmesg|$work/bin/dmesg|g" \
    -e "s|/usr/sbin/wpa_cli|$work/bin/wpa_cli|g" \
    -e "s|/bin/ps|$work/bin/ps|g" \
    -e "s|/sbin/ifconfig|$work/bin/ifconfig|g" \
    -e "s|/usr/bin/netstat|$work/bin/netstat|g" \
    -e "s|/usr/bin/tail|$work/bin/tail|g" "$service" > "$work/service"
chmod 755 "$work/service"
run_service() {
    ember_wifi_diag=YES ember_wifi_diag_interface=aicwf0 \
        ember_wifi_diag_poweroff="$1" "$work/service" start
}
wait_done() {
    count=0
    while ! grep -q final_sync_exit "$1/completed.txt" 2>/dev/null; do
        count=$((count + 1)); [ "$count" -lt 100 ] || exit 1
        sleep 0.1
    done
}
run_service NO
test -f "$work/boot/ember-diag/run-1/snapshot-0/sync.txt"
wait_done "$work/boot/ember-diag/run-1"
test -d "$work/boot/ember-diag/run-1/snapshot-18"
test ! -e "$work/boot/ember-diag/run-1/snapshot-19"
grep -q 'exit=124' "$work/boot/ember-diag/run-1/snapshot-0/dmesg.txt"
test ! -e "$work/powered-off"
cp "$work/boot/ember-diag/run-1/completed.txt" "$work/previous"
run_service YES
wait_done "$work/boot/ember-diag/run-2"
count=0
while [ ! -e "$work/powered-off" ]; do
    count=$((count + 1)); [ "$count" -lt 100 ] || exit 1
    sleep 0.1
done
cmp "$work/previous" "$work/boot/ember-diag/run-1/completed.txt"
touch "$work/unmounted"
if run_service NO > "$work/refusal" 2>&1; then exit 1; fi
test ! -e "$work/boot/ember-diag/run-3"
! grep -E 'wpa_cli .* (scan|reconfigure)$| -K|wpa_supplicant.conf|authorized_keys' "$work/calls"
echo 'PASS: early snapshot, bounded loop/command arguments, failures, unique runs, FAT refusal, opt-in poweroff'
echo 'Command mocks do not prove target timeout handling or physical FAT durability.'
