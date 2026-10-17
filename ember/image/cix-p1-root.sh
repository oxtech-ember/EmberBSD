#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Assemble a reduced static CIX development root. No block device is written.
set -eu
export LC_ALL=C
[ "$#" -eq 5 ] || {
	echo "usage: $0 USERLAND_OUTPUT SOURCE TOOLDIR FIRMWARE NEW_OUTPUT" >&2
	exit 2
}
cohort=$1 src=$2 tools=$3 firmware=$4 out=$5
here=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
profile_src=$(CDPATH= cd -- "$here/../.." && pwd)
for path in "$cohort" "$src" "$tools" "$firmware" "$out"; do
	case "$path" in /*) ;; *) echo 'Absolute paths required.' >&2; exit 2;; esac
	case "$path" in *[!A-Za-z0-9_./-]*) echo 'Unsafe image path.' >&2; exit 2;; esac
done
[ ! -e "$out" ] && [ ! -L "$out" ] || exit 2
[ -s "$cohort/BUILD-INPUTS.txt" ]
[ -s "$cohort/BINARIES-SHA256.txt" ]
revision=$(sed -n 's/^source=//p' "$cohort/BUILD-INPUTS.txt")
case "$revision" in *[!0-9a-f]*|'') exit 2;; esac
[ "${#revision}" -eq 40 ]
[ "$(git -C "$src" rev-parse HEAD)" = "$revision" ]
git -C "$src" diff --quiet
git -C "$src" diff --cached --quiet
profile_revision=$(git -C "$profile_src" rev-parse HEAD)
git -C "$profile_src" diff --quiet
git -C "$profile_src" diff --cached --quiet
# The cohort receipt includes every selected target executable.
checkdir=$(mktemp -d "${TMPDIR:-/tmp}/cix-image.XXXXXX")
trap 'rm -rf "$checkdir"' 0
trap 'exit 1' 1 2 3 15
for dir in bin sbin usr/bin usr/sbin usr/libexec; do
	[ -d "$cohort/dest/$dir" ] && [ ! -L "$cohort/dest/$dir" ] || exit 2
done
find "$cohort/dest/bin" "$cohort/dest/sbin" "$cohort/dest/usr/bin" \
    "$cohort/dest/usr/sbin" "$cohort/dest/usr/libexec" -type f | sort \
    > "$checkdir/files"
awk -F '  ' 'NF != 2 || length($1) != 64 || $1 ~ /[^0-9a-f]/ {exit 1}
    {print $2}' "$cohort/BINARIES-SHA256.txt" > "$checkdir/recorded-raw"
sort "$checkdir/recorded-raw" > "$checkdir/recorded"
cmp "$checkdir/files" "$checkdir/recorded" || {
	echo 'Runtime inventory differs from the verified cohort.' >&2; exit 2
}
# This selected cohort records regular files, including hard-linked aliases.
# A symlink needs a separate verified receipt before this profile can copy it.
find "$cohort/dest/bin" "$cohort/dest/sbin" "$cohort/dest/usr/bin" \
    "$cohort/dest/usr/sbin" "$cohort/dest/usr/libexec" -type l > "$checkdir/links"
[ ! -s "$checkdir/links" ] || {
	echo 'Runtime symlinks have no verified cohort receipt.' >&2; exit 2
}
shasum -a 256 -c "$cohort/BINARIES-SHA256.txt" >/dev/null
[ -s "$cohort/dest/METALOG" ]
awk -v prefix="$cohort/dest/" '{print "./" substr($0, length(prefix) + 1)}' \
    "$checkdir/files" > "$checkdir/relative-files"
awk 'NR == FNR {wanted[$0] = 1; next}
    $1 in wanted {entry[$1] = $0}
    END {for (file in wanted) {
        if (!(file in entry)) {missing = 1; continue}
        print entry[file]
    }; exit missing}' "$checkdir/relative-files" "$cohort/dest/METALOG" \
    > "$checkdir/runtime-metadata-raw"
sort "$checkdir/runtime-metadata-raw" > "$checkdir/runtime.spec"
expected=36b483e37640359a6c686be4661b5f2d94928f93e126a2de09e67c5e04c01b26
[ "$(shasum -a 256 "$firmware" | awk '{print $1}')" = "$expected" ]
for tool in nbmakefs nbmtree nbpwd_mkdb; do [ -x "$tools/bin/$tool" ]; done
for file in sbin/init sbin/ifconfig sbin/fsck sbin/fsck_ffs sbin/mount \
    sbin/mount_tmpfs sbin/shutdown sbin/rndctl usr/sbin/wpa_supplicant \
    usr/sbin/wpa_cli sbin/dhcpcd bin/sh usr/bin/stat usr/libexec/getty usr/sbin/powerd; do
	[ -x "$cohort/dest/$file" ] || {
		echo "Missing runtime input: $file" >&2; exit 2
	}
done
[ -s "$cohort/obj/etc/MAKEDEV" ]
mkdir -p "$out/root"
root=$out/root
for dir in bin sbin usr/bin usr/sbin usr/libexec; do
	mkdir -p "$root/$dir"
	cp -pR "$cohort/dest/$dir/." "$root/$dir/"
done
mkdir -p "$root/etc" "$root/dev" "$root/root" "$root/tmp" "$root/mnt" \
    "$root/var/run" "$root/var/db/dhcpcd" "$root/var/log" \
    "$root/var/spool/lock" "$root/libexec/dhcpcd-hooks" \
    "$root/libdata/firmware/rtwce" "$root/etc/powerd/scripts"
chmod 1777 "$root/tmp"
chmod 700 "$root/root" "$root/var/db/dhcpcd"
cp "$firmware" "$root/libdata/firmware/rtwce/rtw8822c_fw.bin"
cp "$src/sys/dev/ic/rtw88-firmware.LICENSE" \
    "$root/libdata/firmware/rtwce/LICENCE.rtlwifi_firmware.txt"
cp "$src/external/bsd/dhcpcd/dist/hooks/dhcpcd-run-hooks" "$root/libexec/"
cp "$src/external/bsd/dhcpcd/dist/hooks/20-resolv.conf" "$root/libexec/dhcpcd-hooks/"
chmod 755 "$root/libexec/dhcpcd-run-hooks"
cp "$cohort/obj/etc/MAKEDEV" "$root/dev/MAKEDEV"
cp "$here/cix-p1-root.subr" "$root/etc/cix-root.subr"
cp "$here/cix-p1-network.subr" "$root/etc/cix-network.subr"
cp "$here/cix-p1-power.subr" "$root/etc/cix-power.subr"
cp "$here/cix-p1-power-button" "$root/etc/powerd/scripts/power_button"
chmod 755 "$root/etc/powerd/scripts/power_button"
cp "$here/cix-p1-rc" "$root/etc/rc"
cp "$here/cix-p1-rc.shutdown" "$root/etc/rc.shutdown"
cp "$here/cix-p1-console" "$root/usr/libexec/ember-console"
chmod 755 "$root/etc/rc" "$root/etc/rc.shutdown" "$root/usr/libexec/ember-console"
printf 'NAME=ember-cix-root / ffs rw 1 1\n' > "$root/etc/fstab"
printf '127.0.0.1 localhost\n::1 localhost\n' > "$root/etc/hosts"
cp "$src/etc/group" "$root/etc/group"
printf 'root:*:0:0::0:0:Development console:/root:/bin/sh\n' > "$root/etc/master.passwd"
printf 'console "/usr/libexec/getty ember-cix" vt100 on secure\n' > "$root/etc/ttys"
printf 'default:ce:ck:np:\nember-cix:al=root:lo=/usr/libexec/ember-console:sp#115200:np:ce:ck:\n' \
    > "$root/etc/gettytab"
printf '/bin/sh\n' > "$root/etc/shells"
printf 'nohook ntp.conf,ypbind,lookup-hostname,hostname,wpa_supplicant\n' > "$root/etc/dhcpcd.conf"
printf 'group: files\nhosts: files dns\nnetworks: files\npasswd: files\nshells: files\n' \
    > "$root/etc/nsswitch.conf"
printf 'Reduced EmberBSD CIX development root\nuserland=%s\n' "$revision" > "$root/etc/ember-build"
printf 'profile=%s\n' "$profile_revision" >> "$root/etc/ember-build"
"$tools/bin/nbpwd_mkdb" -L -p -d "$root" "$root/etc/master.passwd"
MACHINE=aarch64 MACHINE_ARCH=aarch64 sh "$root/dev/MAKEDEV" -s ramdisk wscons sysmon \
    > "$out/devices-raw.spec"
sed -e '/^\. type=dir/d' -e 's,^\.,./dev,' "$out/devices-raw.spec" > "$out/devices.spec"
"$tools/bin/nbmtree" -c -p "$root" -k type,mode,link > "$out/files.spec"
sed -E 's/(\/set[[:space:]].*)/\1 uid=0 gid=0/' "$out/files.spec" > "$out/root.spec"
cat "$out/devices.spec" >> "$out/root.spec"
cat "$checkdir/runtime.spec" >> "$out/root.spec"
"$tools/bin/nbmakefs" -t ffs -B little -s 2g \
    -o version=2,bsize=16384,fsize=2048 -F "$out/root.spec" -N "$src/etc" \
    "$out/root.fs" "$root"
cp "$cohort/BUILD-INPUTS.txt" "$out/USERLAND-INPUTS.txt"
git -C "$src" diff --quiet
git -C "$src" diff --cached --quiet
[ "$(git -C "$src" rev-parse HEAD)" = "$revision" ]
git -C "$profile_src" diff --quiet
git -C "$profile_src" diff --cached --quiet
[ "$(git -C "$profile_src" rev-parse HEAD)" = "$profile_revision" ]
printf 'userland=%s\nprofile=%s\n' "$revision" "$profile_revision" > "$out/ROOT-INPUTS.txt"
shasum -a 256 "$out/root.fs" > "$out/ROOT-SHA256.txt"
echo 'Created a new 2 GiB FFS image. Provision the private Wi-Fi profile on the target.'
