#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Install a kernel and its board modules from one ember/build-kernel.sh
# output on a running efiboot AArch64 board with one GPT disk. Run it as root
# on the board; reboot into the new kernel before the next installation.
#
# The default boot.cfg entry selects the file, as in efiboot (efiblock.c):
# `boot FILE` reads the first NetBSD FFS partition of the disk, `boot hd0x:FILE`
# reads GPT entry x. That file must be the running kernel; it becomes
# netbsd.recovery on the same partition, and boot.cfg gets the new kernel, the recovery
# kernel in single-user mode and a command prompt.
#
# usage: install-kernel.sh BUILD_OUTPUT [CONFIG]
# EMBER_ROOT (default empty) prefixes the mount points, for the test.
set -euf
fail() { echo "install-kernel.sh: $*" >&2; exit 1; }
[ "$#" -ge 1 ] && [ "$#" -le 2 ] || fail 'usage: install-kernel.sh BUILD_OUTPUT [CONFIG]'
build=$1
config=${2:-EMBER64}
root=${EMBER_ROOT:-}
kernel=$build/netbsd-$config
cfg=$root/boot/boot.cfg
[ -f "$kernel" ] || fail "no $kernel"
[ -f "$cfg" ] || fail "no $cfg"
release=$(grep -a -o -m1 'NetBSD [0-9][0-9]*\.[0-9][0-9]*' "$kernel" | cut -d' ' -f2)
[ -n "$release" ] || fail "$kernel is not a NetBSD kernel"

# The kernel file of the default entry.
settings=$(sed -e 's/\r$//' -e 's/#.*//' "$cfg")
default=$(printf '%s\n' "$settings" | sed -n 's/^default=\([0-9][0-9]*\).*/\1/p' | tail -1)
command=$(printf '%s\n' "$settings" | grep '^menu=' | sed -n "${default:-1}p")
set -- ${command#*:}
[ "$#" -ge 2 ] && [ "$1" = boot ] || fail "the default entry boots no kernel: $command"
file=$2
shift 2
arguments=$*
case $file in
hd0[a-p]:*) letter=${file%%:*}; letter=${letter#hd0}; file=${file#*:}
	device=hd0$letter:; entry=$(( $(printf '%d' "'$letter") - 96 )) ;;
*:*) fail "cannot match the device of: boot $file" ;;
*) device= entry= ;;
esac

# The one disk with the loader volume, and its partition.
disks=$(sysctl -n hw.disknames | tr ' ' '\n' | grep -Ev '^(dk|md|vnd|cgd|raid|ccd)[0-9]' || :)
[ "$(printf '%s\n' "$disks" | grep -c .)" -eq 1 ] || fail "several disks: $(echo $disks)"
part=$(gpt show "$disks" | awk -v e="$entry" '$4 == "GPT" && $3 ~ /^[0-9]+$/ &&
	(e == "" ? / NetBSD FFS/ : $3 == e) {print $3, $1, $0}' | sort -n | head -1)
case $part in *' NetBSD FFS'*) ;; *) fail "the default entry does not boot from a NetBSD FFS partition of $disks" ;; esac
start=$(echo "$part" | cut -d' ' -f2)
wedge=$(dkctl "$disks" listwedges | awk -v s="$start" 'index($0, " blocks at " s ",") {sub(":$", "", $1); print $1}')
mountpoint=$(mount | awk -v d="/dev/$wedge" '$1 == d {print $3}')
[ -n "$wedge" ] && [ -n "$mountpoint" ] || fail "the partition at block $start of $disks is not mounted"
directory=$root${mountpoint%/}
current=$directory/${file#/}
recovery=$directory/netbsd.recovery
[ -f "$current" ] && [ ! "$current" -ef "$recovery" ] ||
    fail "the default entry must boot a kernel other than netbsd.recovery: $current"
running=$(sysctl -n kern.version | head -1)
grep -a -q -F "$running" "$current" ||
    fail "$current is not the running kernel ($running); if netbsd.recovery runs, copy it to $current first"

copy() { mkdir -p "$(dirname "$2")"; cp "$1" "$2.new"; sync; mv "$2.new" "$2"; }
# Keep a bootable recovery entry at each step.
copy "$current" "$recovery"
[ -f "$cfg.before-install" ] || cp "$cfg" "$cfg.before-install"
{
	printf '%s\n' "$settings" | grep -v -e '^menu=' -e '^default=' -e '^timeout=' -e '^$' || :
	echo "menu=Boot EmberBSD:boot $device$file${arguments:+ $arguments}"
	echo "menu=Recover previous kernel (single user):boot ${device}netbsd.recovery -s${arguments:+ $arguments}"
	echo 'menu=Command prompt:prompt'
	echo 'default=1'
	echo 'timeout=5'
} > "$cfg.tmp"
copy "$cfg.tmp" "$cfg"
rm -f "$cfg.tmp"
copy "$kernel" "$current"
set +f
for module in "$build"/*.kmod; do
	[ -f "$module" ] || continue
	name=$(basename "$module" .kmod)
	copy "$module" "$root/stand/$(uname -m)/$release/modules/$name/$name.kmod"
done
sync
echo "installed $config ($release) as $current on $wedge; reboot with shutdown -r"
