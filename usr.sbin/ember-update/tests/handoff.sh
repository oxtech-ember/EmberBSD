#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Exercise the native transition offline inside a disposable NetBSD chroot.
# INPUT contains the original release.json, etc.tar.xz, legacy-profile.json
# and control.json, the signed base's seal.json, and the expected shell
# sysctl outputs buildinfo and version. Supply a profile name as argument 3.
set -eu
[ "$#" -eq 4 ] || { echo 'usage: handoff.sh CLIENT INPUT PROFILE RC_SCRIPT' >&2; exit 2; }
[ "$(id -u)" -eq 0 ] && [ "$(uname -s)" = NetBSD ] || exit 2
client=$1 input=$2 profile=$3 script=$4
case "$profile" in *[!a-z0-9._-]*|'') exit 2;; esac
root=$(mktemp -d /var/tmp/test-update-handoff.XXXXXXXX)
trap 'rm -rf "$root"' EXIT HUP INT TERM
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
for leaf in /bin/sh /bin/cat /bin/cp /bin/mv /bin/rm /usr/bin/grep /usr/bin/awk /usr/bin/mktemp; do
	utility "$leaf"
	ldd "$leaf" | awk '$2 == "=>" && $3 ~ /^\// { print $3 }' | while read -r lib; do utility "$lib"; done
done
install -m 0555 "$client" "$root/usr/sbin/ember-update"
ldd "$client" 2>/dev/null | awk '$2 == "=>" && $3 ~ /^\// { print $3 }' | while read -r lib; do utility "$lib"; done
cp /etc/rc.subr "$root/etc/rc.subr"
cp /etc/defaults/rc.conf "$root/etc/defaults/rc.conf"
printf '. /etc/defaults/rc.conf\nember_update_reboot=ask\n' >"$root/etc/rc.conf"
printf 'ember_os_update=YES\n' >"$root/etc/rc.conf.d/ember_os_update"
printf 'ember_pkg_update=NO\n' >"$root/etc/rc.conf.d/ember_pkg_update"
printf 'ember_update_reboot=ask\n' >"$root/etc/rc.conf.d/ember_update"
install -m 0555 "$script" "$root/etc/rc.d/ember_update"
cp "$input/control.json" "$root/etc/ember-os-update.json"
cp "$input/legacy-profile.json" "$root/etc/ember-uboot-slots.json"
cp "$input/seal.json" "$root/usr/share/ember-update/handoff/$profile.json"
for f in release.json etc.tar.xz; do install -m 0600 "$input/$f" "$root/var/db/ember-os-update/$f"; done
cp "$input/buildinfo" "$root/buildinfo"
cp "$input/version" "$root/version"
: >"$root/dev/null"
cat >"$root/sbin/sysctl" <<'SH'
#!/bin/sh
case "$2" in kern.buildinfo) cat /buildinfo ;; kern.version) cat /version ;; *) exit 2 ;; esac
SH
cat >"$root/usr/libexec/ember-os-slot" <<'SH'
#!/bin/sh
case "$1" in
status)
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
printf '#!/bin/sh\n[ ! -f /unhealthy ]\n' >"$root/etc/ember-os-health"
chmod 0555 "$root/sbin/sysctl" "$root/usr/libexec/ember-os-slot" "$root/etc/ember-os-health"
sha=$(sha256 -q "$input/release.json")
step() { /usr/sbin/chroot "$root" /usr/sbin/ember-update handoff; }
refused()
{
	if step >"$root/failure" 2>&1; then echo 'Unexpected migration success' >&2; exit 1; fi
	if ! grep -Fq "$1" "$root/failure"; then cat "$root/failure" >&2; exit 1; fi
}
model() { echo "$*" >"$root/model"; }
model B A null true B "$sha"
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
touch "$root/unhealthy"
refused "health check failed"
rm "$root/unhealthy"
cp "$root/buildinfo" "$root/original-buildinfo"
printf 'local kernel\n' >"$root/buildinfo"
refused "trial kernel differs"
mv "$root/original-buildinfo" "$root/buildinfo"
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
step
grep -Fq "B B null true B $sha" "$root/model"
test ! -e "$root/etc/ember-os-update.json"
cmp "$root/var/db/ember-update/os-release.json" "$input/release.json"
test ! -e "$root/usr/share/ember-update/handoff/$profile.json"
echo 'Confirmed the legacy cohort, including interruption recovery and exact defaults.'
