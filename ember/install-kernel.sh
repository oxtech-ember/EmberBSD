#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Install a kernel and its board modules from one ember/build-kernel.sh
# output. A provisioned two-slot system uses its update controller.
# On a single-root efiboot AArch64 board, install to the selected GPT disk.
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
# An enrolled system owns trial selection, OS receipts and confirmation.
# A missing controller or an incomplete build must keep the current boot path.
profile=$root/etc/ember-update-profile.json
mode=single
if [ -e "$profile" ] || [ -L "$profile" ]; then
	controller=$root/usr/sbin/ember-update
	[ -x "$controller" ] || fail "missing installed controller: $controller"
	mode=$("$controller" kernel-mode) || fail 'invalid installed update profile'
	case "$mode" in
	single|slots) ;;
	*) fail 'invalid installed kernel mode' ;;
	esac
fi
if [ "$mode" = slots ]; then
	stage=$(mktemp -d "$root/var/tmp/ember-kernel.XXXXXXXX") || fail 'cannot stage kernel'
	trap 'rm -rf "$stage"' 0
	trap 'exit 1' HUP INT TERM
	cp "$kernel" "$stage/netbsd"
	mkdir "$stage/modules"
	set +f
	for module in "$build"/*.kmod; do
		[ -f "$module" ] || continue
		cp "$module" "$stage/modules/"
	done
	if [ -f "$build/netbsd-$config.img" ]; then
		cp "$build/netbsd-$config.img" "$stage/netbsd.img"
	fi
	for tree in "$build"/*.dtb; do
		[ -f "$tree" ] || continue
		cp "$tree" "$stage/"
	done
	"$controller" apply-kernel "$stage"
	exit
fi
[ -f "$cfg" ] || fail "no $cfg"
# The board base has od and awk. Read the ELF header and the program table
# in file byte order. Stop before scanning the kernel payload.
check_kernel() (
	size=$(wc -c < "$1") || exit 1
	od -An -v -tu1 "$1" | LC_ALL=C awk -v size="$size" '
	function le(at, n,    i, value) {
		for (i = at + n - 1; i >= at; i--) value = value * 256 + byte[i]
		return value + 0
	}
	function fits(start, len) { return start <= size && len <= size - start }
	function header(    sections) {
		phoff = le(32, 8); phnum = le(56, 2)
		entrylo = le(24, 4); entryhi = le(28, 4)
		sections = le(60, 2)
		return sprintf("%c%c%c%c", byte[0], byte[1], byte[2], byte[3]) == "\177ELF" &&
		    byte[4] == 2 && byte[5] == 1 && byte[6] == 1 &&
		    le(16, 2) == 2 && le(18, 2) == 183 && le(20, 4) == 1 &&
		    le(52, 2) == 64 && le(54, 2) == 56 && phnum > 0 &&
		    phoff >= 64 && fits(phoff, 56 * phnum) &&
		    (!sections || (le(58, 2) == 64 && fits(le(40, 8), 64 * sections)))
	}
	# Check whether an ELF64 load range wraps, without rounding its address.
	function wraps(at,    carry) {
		carry = le(at, 4) + le(40, 4) >= 4294967296
		return le(at + 4, 4) + le(44, 4) + carry >= 4294967296
	}
	function segment(    offset, filesz, delta) {
		if (le(0, 4) != 1) return 1
		offset = le(8, 8); filesz = le(32, 8)
		if (filesz > le(40, 8) || (filesz && !fits(offset, filesz)) ||
		    wraps(16) || wraps(24)) return 0
		# Subtract high and low words separately for high kernel addresses.
		delta = (entryhi - le(20, 4)) * 4294967296 + entrylo - le(16, 4)
		if (le(4, 4) % 2 && delta >= 0 && delta < filesz) executable = 1
		return 1
	}
	BEGIN { pos = -1; size += 0 }
	{
		for (i = 1; i <= NF; i++) {
			pos++
			if (pos < 64) {
				byte[pos] = $i
				if (pos == 63 && !header()) exit
			} else if (pos >= phoff) {
				field = (pos - phoff) % 56
				byte[field] = $i
				if (field == 55) {
					if (!segment()) exit
					if (++seen == phnum) { valid = executable; exit }
				}
			}
		}
	}
	END { exit !valid }'
)
check_kernel "$kernel" || fail "$kernel is not a loadable little-endian AArch64 ELF64 executable"

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

checked_copy() {
	cp "$1" "$2" || fail "could not copy $1"
	sync
	cmp -s "$1" "$2" || fail "copy of $1 differs"
}
copy() (
	parent=$(dirname "$2")
	mkdir -p "$parent"
	tmp=$(mktemp -d "$parent/.ember-copy.XXXXXXXX") || exit 1
	trap 'rm -rf "$tmp"' 0
	trap 'exit 1' HUP INT TERM
	checked_copy "$1" "$tmp/file"
	mv -f "$tmp/file" "$2"
)

# Stage on the same filesystem as the active kernel. Check the bytes that
# will be renamed before changing recovery, the menu, or the modules.
stage=$(mktemp -d "$(dirname "$current")/.ember-kernel.XXXXXXXX") || fail 'could not stage kernel'
trap 'rm -rf "$stage"' 0
trap 'exit 1' HUP INT TERM
checked_copy "$kernel" "$stage/kernel"
check_kernel "$stage/kernel" || fail 'invalid staged kernel'
# newvers.sh includes this kernel identification in reproducible builds too.
release=$(LC_ALL=C grep -a -E -o -m1 '@\(#\)NetBSD [0-9]+\.[0-9]+[[:alnum:]_.-]* \([^()[:cntrl:]]+\)' "$stage/kernel" |
	sed -nE '1s/.*NetBSD ([0-9]+\.[0-9]+).*/\1/p')
[ -n "$release" ] || fail "$kernel has no NetBSD kernel identification"

# Keep a bootable recovery entry at each step.
copy "$current" "$recovery"
[ -f "$cfg.before-install" ] || copy "$cfg" "$cfg.before-install"
{
	printf '%s\n' "$settings" | grep -v -e '^menu=' -e '^default=' -e '^timeout=' -e '^$' || :
	echo "menu=Boot EmberBSD:boot $device$file${arguments:+ $arguments}"
	echo "menu=Recover previous kernel (single user):boot ${device}netbsd.recovery -s${arguments:+ $arguments}"
	echo 'menu=Command prompt:prompt'
	echo 'default=1'
	echo 'timeout=5'
} > "$stage/boot.cfg"
copy "$stage/boot.cfg" "$cfg"
mv -f "$stage/kernel" "$current"
set +f
for module in "$build"/*.kmod; do
	[ -f "$module" ] || continue
	name=$(basename "$module" .kmod)
	copy "$module" "$root/stand/$(uname -m)/$release/modules/$name/$name.kmod"
done
sync
echo "installed $config ($release) as $current on $wedge; reboot with shutdown -r"
