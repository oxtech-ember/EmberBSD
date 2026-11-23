#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Install a kernel and its board modules from one ember/build-kernel.sh
# output on a running efiboot AArch64 board. The kernel replaces the file
# of the default boot.cfg entry; the first replaced kernel stays as
# netbsd.recovery. boot.cfg keeps its other settings and gets three menu
# entries: the new kernel, the recovery kernel in single-user mode and a
# command prompt. Run it as root on the board; reboot afterwards.
#
# usage: install-kernel.sh BUILD_OUTPUT [CONFIG]
# EMBER_ROOT (default /) and EMBER_BOOT (default /boot, the mounted loader
# volume with boot.cfg) exist for the test.
set -euf
fail() { echo "install-kernel.sh: $*" >&2; exit 1; }
[ "$#" -ge 1 ] && [ "$#" -le 2 ] || fail 'usage: install-kernel.sh BUILD_OUTPUT [CONFIG]'
build=$1
config=${2:-EMBER64}
root=${EMBER_ROOT:-/}
loader=${EMBER_BOOT:-/boot}
cfg=$loader/boot.cfg
kernel=$build/netbsd-$config
[ -f "$kernel" ] || fail "no $kernel"
[ -f "$cfg" ] || fail "no $cfg"

# Check everything before the first write.
# efiboot loads an ELF64 little-endian AArch64 executable: ET_EXEC, EM_AARCH64.
header=$(od -An -tx1 -N20 "$kernel" | tr -d ' \n')
case $header in 7f454c460201*0200b700) ;; *) fail "$kernel is not an AArch64 ELF64 kernel" ;; esac
release=$(grep -a -o -m1 'NetBSD [0-9][0-9]*\.[0-9][0-9]*' "$kernel" | head -1 | cut -d' ' -f2)
[ -n "$release" ] || fail "$kernel has no NetBSD release string"

# Read boot.cfg as efiboot does: no comments, the last default=, clamped.
settings=$(sed -e 's/\r$//' -e 's/#.*//' "$cfg")
menus=$(printf '%s\n' "$settings" | grep '^menu=' || :)
count=$(printf '%s\n' "$menus" | grep -c . || :)
[ "$count" -gt 0 ] || fail "$cfg has no menu entry"
default=$(printf '%s\n' "$settings" | sed -n 's/^default=\([0-9][0-9]*\).*/\1/p' | tail -1)
[ -n "$default" ] && [ "$default" -ge 1 ] || default=1
[ "$default" -le "$count" ] || default=$count
command=$(printf '%s\n' "$menus" | sed -n "${default}p")
command=${command#*:}
case $command in *';'*) fail "the default entry runs several commands: $command" ;; esac
set -- $command
[ "$#" -ge 2 ] && [ "$1" = boot ] || fail "the default entry boots no kernel: $command"
file=$2
shift 2
arguments=$*
# A file without a device is on the loader volume; a device names the root.
case $file in
*:*) device=${file%%:*}: directory=$root; file=${file#*:} ;;
*) device= directory=$loader ;;
esac
file=${file#/}
current=$directory/$file
[ -f "$current" ] || fail "the default entry boots $current, which is absent"
recovery=$directory/netbsd.recovery
old_release=$(grep -a -o -m1 'NetBSD [0-9][0-9]*\.[0-9][0-9]*' "$current" | head -1 | cut -d' ' -f2 || :)

copy() {
	mkdir -p "$(dirname "$2")"
	cp "$1" "$2.new"
	sync
	cmp -s "$1" "$2.new" || fail "copy of $1 differs"
	mv "$2.new" "$2"
}

# Keep the first replaced kernel; a later installation does not replace it.
if [ -f "$recovery" ]; then
	echo "keeping $recovery"
else
	copy "$current" "$recovery"
fi
copy "$kernel" "$current"
modules=$root/stand/$(uname -m)/$release/modules
set +f
for module in "$build"/*.kmod; do
	[ -f "$module" ] || continue
	name=$(basename "$module" .kmod)
	copy "$module" "$modules/$name/$name.kmod"
done
set -f

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
# Some images keep a copy for the removable-media loader path.
[ ! -f "$loader/EFI/BOOT/boot.cfg" ] || copy "$cfg" "$loader/EFI/BOOT/boot.cfg"
sync
echo "installed $config ($release) as $current"
echo "previous boot.cfg: $cfg.before-install; reboot to use the kernel"
if [ "$old_release" = "$release" ]; then
	echo "the board modules of release $release are now the new ones:"
	echo "stay in single-user mode on the recovery kernel"
fi
