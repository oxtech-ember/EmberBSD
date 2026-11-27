#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Exercise the native transition offline inside a disposable NetBSD chroot.
# INPUT contains the original release.json, etc.tar.xz, legacy-profile.json
# and control.json, the signed base's seal.json, and the expected shell
# sysctl outputs buildinfo and version. Supply a profile name as argument 3.
set -eu
case "$#:${5:-}" in 4:|5:--rc-start) ;; *)
	echo 'usage: handoff.sh CLIENT INPUT PROFILE RC_SCRIPT [--rc-start]' >&2; exit 2 ;;
esac
[ "$(id -u)" -eq 0 ] && [ "$(uname -s)" = NetBSD ] || exit 2
client=$1 input=$2 profile=$3 script=$4
case "$profile" in *[!a-z0-9._-]*|'') exit 2;; esac
root=$(mktemp -d /var/tmp/test-update-handoff.XXXXXXXX)
updater=
watcher=
cleanup()
{
	if [ -n "$updater" ]; then kill -TERM "$updater" 2>/dev/null || :; wait "$updater" || :; fi
	if [ -n "$watcher" ]; then kill -TERM "$watcher" 2>/dev/null || :; fi
	rm -rf "$root"
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM
mkdir -p "$root"/etc/rc.conf.d "$root"/etc/defaults "$root"/dev "$root"/var/run "$root"/var/db/ember-os-update \
    "$root"/usr/libexec "$root"/usr/sbin "$root"/etc/rc.d "$root"/sbin "$root"/usr/share/ember-update/handoff
# Copy the complete runtime of each utility. The chroot has no device nodes.
utility()
{
	leaf=$1
	mkdir -p "$root${leaf%/*}"
	cp -pL "$leaf" "$root$leaf"
}
utility /libexec/ld.elf_so
for leaf in /bin/sh /bin/cat /bin/cp /bin/mv /bin/rm /bin/sleep /usr/bin/grep /usr/bin/awk /usr/bin/mktemp; do
	utility "$leaf"
	ldd "$leaf" | awk '$2 == "=>" && $3 ~ /^\// { print $3 }' | while read -r lib; do utility "$lib"; done
done
install -m 0555 "$client" "$root/usr/sbin/ember-update"
ldd "$client" 2>/dev/null | awk '$2 == "=>" && $3 ~ /^\// { print $3 }' | while read -r lib; do utility "$lib"; done
cp /etc/rc.subr "$root/etc/rc.subr"
cp /etc/defaults/rc.conf "$root/etc/defaults/rc.conf"
printf '. /etc/defaults/rc.conf\nember_update_reboot=ask\n' >"$root/etc/rc.conf"
cp "$root/etc/rc.conf" "$root/initial-rc.conf"
printf 'ember_os_update=YES\n' >"$root/etc/rc.conf.d/ember_os_update"
printf 'ember_pkg_update=NO\n' >"$root/etc/rc.conf.d/ember_pkg_update"
printf 'ember_update_reboot=ask\n' >"$root/etc/rc.conf.d/ember_update"
install -m 0555 "$script" "$root/etc/rc.d/ember_update"
# The standard set has no former rc scripts. The transition set has an inert one.
printf '# PROVIDE: LOGIN NETWORKING syslogd certctl_init\n' >"$root/services"
check_order()
{
	/sbin/rcorder "$root/services" "$root"/etc/rc.d/* >"$root/order" 2>"$root/errors" ||
	    { cat "$root/errors" >&2; exit 1; }
	[ ! -s "$root/errors" ] || { cat "$root/errors" >&2; exit 1; }
	awk -v services="$root/services" -v native="$root/etc/rc.d/ember_update" '
	    $0 == services { ready = 1 }
	    $0 == native && ready { found = 1 }
	    END { exit !found }' "$root/order"
}
check_order
printf '#!/bin/sh\nexit 0\n' >"$root/etc/rc.d/ember_os_update"
chmod 0555 "$root/etc/rc.d/ember_os_update"
check_order
cp "$input/control.json" "$root/etc/ember-os-update.json"
cp "$input/legacy-profile.json" "$root/etc/ember-uboot-slots.json"
cp "$input/seal.json" "$root/usr/share/ember-update/handoff/$profile.json"
for f in release.json etc.tar.xz; do install -m 0600 "$input/$f" "$root/var/db/ember-os-update/$f"; done
# newvers stores one newline. sysctl -n adds another when it prints the value.
buildinfo=$(cat "$input/buildinfo")
printf '%s\n' "$buildinfo" >"$root/buildinfo"
cp "$input/version" "$root/version"
: >"$root/dev/null"
cat >"$root/sbin/sysctl" <<'SH'
#!/bin/sh
case "$1 $2" in
'-r kern.buildinfo') cat /buildinfo; printf '\0' ;;
'-n kern.buildinfo') cat /buildinfo; printf '\n' ;;
'-n kern.version') cat /version ;;
*) exit 2 ;;
esac
SH
cat >"$root/usr/libexec/ember-os-slot" <<'SH'
#!/bin/sh
case "$1" in
status)
	[ -f /etc/ember-os-update.json ] || echo status >>/native-status
	read -r running confirmed next trial slot sha </model
	printf '{"running":"%s","confirmed":"%s","next":%s,"trial":%s,"prepared":{"slot":"%s","sha256":"%s"}}\n' \
	    "$running" "$confirmed" "$next" "$trial" "$slot" "$sha"
	;;
confirm)
	read -r running confirmed next trial slot sha </model
	[ "$2" = "$running" ] && [ "$slot" = "$running" ] && [ "$next" = null ] && [ "$trial" = true ] || exit 2
	echo "$running $running null true $slot $sha" >/model
	[ ! -e /interrupt ] || exit 1
	;;
*) exit 2;;
esac
SH
cat >"$root/etc/ember-os-health" <<'SH'
#!/bin/sh
[ -f /health-mode ] || exit 0
read -r mode </health-mode
echo probe >>/probes
case "$mode" in
route) [ -f /route ] ;;
unhealthy) exit 1 ;;
fatal) exit 2 ;;
signal) kill -TERM "$$" ;;
hang)
	if [ ! -f /retried ]; then sleep 10; : >/retried; exit 1; fi
	sleep 120 & echo "$!" >/sleeper
	wait ;;
esac
SH
chmod 0555 "$root/sbin/sysctl" "$root/usr/libexec/ember-os-slot" "$root/etc/ember-os-health"
sha=$(sha256 -q "$input/release.json")
step() { "$@" /usr/sbin/chroot "$root" /usr/sbin/ember-update handoff; }
refused()
{
	detail=$1; shift
	if step "$@" >"$root/failure" 2>&1; then echo 'Unexpected migration success' >&2; exit 1; fi
	if ! grep -Fq "$detail" "$root/failure"; then cat "$root/failure" >&2; exit 1; fi
}
model() { echo "$*" >"$root/model"; }
await_probe()
{
	for delay in 1 2 3 4 5 6 7 8 9 10; do [ ! -s "$1" ] || return 0; sleep 1; done
	echo "Probe did not write $1" >&2; return 1
}
check_reaped()
{
	read -r sleeper <"$root/sleeper"
	for delay in 1 2 3 4 5; do kill -0 "$sleeper" 2>/dev/null || return 0; sleep 1; done
	echo 'Hook child survived' >&2; return 1
}
rc_start_cases()
{
	pidfile=$root/var/run/ember_update.pid
	for policy in inherited rc.conf drop-in; do
		cp "$input/control.json" "$root/etc/ember-os-update.json"
		cp "$input/seal.json" "$root/usr/share/ember-update/handoff/$profile.json"
		rm -f "$root/etc/ember-os-update.migrated.json" "$root/var/db/ember-update/handoff.json" \
		    "$root/native-status" "$root/health-mode"
		cp "$root/initial-rc.conf" "$root/etc/rc.conf"
		printf 'ember_update_reboot=ask\n' >"$root/etc/rc.conf.d/ember_update"
		case "$policy" in
		rc.conf) echo ember_update=NO >>"$root/etc/rc.conf" ;;
		drop-in) echo ember_update=NO >>"$root/etc/rc.conf.d/ember_update" ;;
		esac
		model B A null true B "$sha"
		# Reproduce the shell and cached defaults that /etc/rc passes to the service.
		/usr/sbin/chroot "$root" /bin/sh -ec '
		    . /etc/rc.subr
		    . /etc/rc.conf
		    _rc_conf_loaded=true
		    rc_fast=yes
		    checkyesno ember_update && exit 1
		    run_rc_script /etc/rc.d/ember_update start
		' >"$root/rc-start.log" 2>&1 || { cat "$root/rc-start.log" >&2; return 1; }
		grep -Fxq "B B null true B $sha" "$root/model"
		test ! -e "$root/etc/ember-os-update.json"
		if [ "$policy" = inherited ]; then
			grep -Fq 'Starting ember_update.' "$root/rc-start.log"
			await_probe "$pidfile"
			read -r watcher <"$pidfile"
			kill -0 "$watcher"
			ps -p "$watcher" -o args= | grep -Fq '/usr/sbin/ember-update watch'
			await_probe "$root/native-status"
			kill -TERM "$watcher"
			for delay in 1 2 3 4 5; do [ -f "$pidfile" ] || break; sleep 1; done
			test ! -e "$pidfile"; watcher=
		else
			test ! -e "$pidfile"
			test -s "$root/native-status"	# boot reconciled the slot.
			if grep -Fq 'Starting ember_update.' "$root/rc-start.log"; then return 1; fi
		fi
	done
	echo 'First rc start applied inherited and explicit site policies.'
}
if [ "${5:-}" = --rc-start ]; then rc_start_cases; exit; fi
model B A null true B "$sha"
for former in ember_os_update ember_pkg_update; do
	refused "a legacy update service is active" /usr/bin/flock "$root/var/run/$former.pid"
done
printf x >>"$root/var/db/ember-os-update/etc.tar.xz"
refused "legacy release, defaults, or hardware differs"
cp "$input/etc.tar.xz" "$root/var/db/ember-os-update/etc.tar.xz"
model B A null true B "$(printf '%064d' 0)"
refused "consumed and identified trial"
model B B null false B "$sha"
refused "consumed and identified trial"
model B A '"A"' true B "$sha"
refused "invalid boot slot status"
model B A null true B "$sha"
# A persistent readiness failure must expire. Hook errors and signals are final.
for mode in unhealthy fatal signal; do
	echo "$mode" >"$root/health-mode"; : >"$root/probes"
	if [ "$mode" = unhealthy ]; then
		refused "trial health check timed out" /usr/bin/timeout 38
		[ "$(wc -l <"$root/probes")" -gt 1 ]
	else
		refused "trial health check failed" /usr/bin/timeout 10
		[ "$(wc -l <"$root/probes")" -eq 1 ]
	fi
	grep -Fxq "B A null true B $sha" "$root/model"
done
# A hung retry gets the remainder of the original 30 seconds.
echo hang >"$root/health-mode"
refused "trial health check timed out" /usr/bin/timeout 38
check_reaped
grep -Fxq "B A null true B $sha" "$root/model"
# Stop while the hook is running. The old slot stays confirmed.
rm "$root/sleeper"
/usr/sbin/chroot "$root" /usr/sbin/ember-update handoff & updater=$!
await_probe "$root/sleeper"
kill -TERM "$updater"
wait "$updater"; updater=
check_reaped
grep -Fxq "B A null true B $sha" "$root/model"
rm "$root/health-mode"
chmod 0777 "$root/etc/ember-os-health"
refused "unexpected owner, mode, or file type"
chmod 0555 "$root/etc/ember-os-health"
cp "$root/buildinfo" "$root/original-buildinfo"
printf 'local kernel\n' >"$root/buildinfo"
refused "trial kernel differs"
printf 'EmberBSD:%040d\n' 0 >"$root/buildinfo"
refused "trial kernel differs"
cat "$root/original-buildinfo" >"$root/buildinfo"
printf 'extra identity\n' >>"$root/buildinfo"
refused "trial kernel differs"
cat "$root/original-buildinfo" >"$root/buildinfo"
printf '\n' >>"$root/buildinfo"
refused "trial kernel differs"
mv "$root/original-buildinfo" "$root/buildinfo"
mv "$root/version" "$root/original-version"
printf 'NetBSD 11.1 (CUSTOM) #0\n' >"$root/version"
refused "trial kernel differs"
mv "$root/original-version" "$root/version"
printf 'prior unrelated archive
' >"$root/etc/ember-os-update.migrated.json"
refused "different archived legacy configuration"
grep -Fq "B A null true B $sha" "$root/model"
rm "$root/etc/ember-os-update.migrated.json"
[ ! -e "$root/var/db/ember-update/handoff.json" ]
touch "$root/interrupt"
refused "cannot confirm the migrated trial"
grep -Fq "B B null true B $sha" "$root/model"
rm "$root/interrupt"
# Resume after the durable primary marker, even after consuming the public seal.
rm "$root/usr/share/ember-update/handoff/$profile.json"
model B B null false B "$sha"
step
for f in os-release.json installed.json; do cmp "$root/var/db/ember-update/$f" "$input/release.json"; done
cmp "$root/var/db/ember-update/etc.tar.xz" "$input/etc.tar.xz"
test ! -e "$root/etc/ember-os-update.json"
cmp "$root/etc/ember-os-update.migrated.json" "$input/control.json"
grep -Fxq ember_update=YES "$root/etc/rc.conf"
grep -Fxq ember_update_packages=NO "$root/etc/rc.conf"
grep -Fxq ember_update_os=YES "$root/etc/rc.conf"
grep -Fxq ember_update_reboot=ask "$root/etc/rc.conf"
# Exercise an uninterrupted confirmation on a second candidate state.
cp "$input/seal.json" "$root/usr/share/ember-update/handoff/$profile.json"
mv "$root/etc/ember-os-update.migrated.json" "$root/etc/ember-os-update.json"
rm "$root/var/db/ember-update/handoff.json"
model B A null true B "$sha"
# DHCP can install its route after the first local readiness probe.
echo route >"$root/health-mode"; : >"$root/probes"
/usr/bin/timeout 38 /usr/sbin/chroot "$root" /usr/sbin/ember-update handoff & updater=$!
await_probe "$root/probes"
sleep 2
: >"$root/route"
wait "$updater"; updater=
[ "$(wc -l <"$root/probes")" -gt 1 ]
grep -Fq "B B null true B $sha" "$root/model"
test ! -e "$root/etc/ember-os-update.json"
cmp "$root/var/db/ember-update/os-release.json" "$input/release.json"
test ! -e "$root/usr/share/ember-update/handoff/$profile.json"
echo 'Confirmed the legacy cohort, including interruption recovery and exact defaults.'
rc_start_cases
