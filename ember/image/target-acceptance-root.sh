#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Assemble a disposable read-only FFS root using tools from one OS build.
set -eu

usage()
{
	echo "Usage: $0 ROOT_TREE DESTINATION TOOLDIR OS_SOURCE ROOT_DEVICE IMAGE HOST_SH" >&2
	exit 2
}

[ "$#" -eq 7 ] || usage
root=$1
destination=$2
tooldir=$3
os_source=$4
root_device=$5
image=$6
host_sh=$7

case $root_device in
	ld*[a-p])
		number=${root_device#ld}
		partition=${number#${number%?}}
		unit=${number%?}
		case $unit in ''|*[!0-9]*) echo "invalid root device: $root_device" >&2; exit 2;; esac
		;;
	*) echo "invalid root device: $root_device" >&2; exit 2;;
esac

case $image in /*) ;; *) echo "image path must be absolute" >&2; exit 2;; esac
[ -d "$root" ] && [ -d "$destination/etc" ] && [ -d "$tooldir/bin" ] &&
	[ -d "$os_source/etc" ] || { echo "incomplete selected OS build inputs" >&2; exit 1; }
[ -x "$tooldir/bin/nbmtree" ] && [ -x "$tooldir/bin/nbmakefs" ] ||
	{ echo "same-build nbmtree/nbmakefs are required" >&2; exit 1; }
[ -f "$destination/etc/MAKEDEV" ] && [ -x "$destination/etc/MAKEDEV" ] ||
	{ echo "same-build MAKEDEV is missing" >&2; exit 1; }
[ ! -e "$image" ] || { echo "image output already exists" >&2; exit 1; }

work=${image}.spec.$$
cleanup()
{
	rm -f "$work" "${work}.new" "${work}.devices" "${work}.devspec"
}
trap cleanup EXIT HUP INT TERM

# nbmtree emits paths relative to the root. MAKEDEV -s emits device metadata;
# translate it into the dev/ subtree expected by makefs. The FFS spec always
# starts with root ownership, independent of the host user or nbmtree defaults.
"$tooldir/bin/nbmtree" -c -p "$root" -k type,mode,link > "$work"
MACHINE=aarch64
MACHINE_ARCH=aarch64
NETBSDSRCDIR=$os_source
export MACHINE MACHINE_ARCH NETBSDSRCDIR
"$host_sh" "$destination/etc/MAKEDEV" \
	-s "ld${unit}" > "${work}.devices"
sed -e '/^\. type=dir/d' -e 's,^[.]/,./dev/,' \
	"${work}.devices" > "${work}.devspec"
device_path="./dev/ld${unit}${partition}"
grep -q "^${device_path} type=block" "${work}.devspec" || {
	echo "MAKEDEV did not describe selected root device $root_device" >&2
	exit 1
}
{
	echo '/set uid=0 gid=0'
	awk '
		$1 == "/set" {
			line = "/set"
			for (i = 2; i <= NF; i++)
				if ($i !~ /^(uid|gid)=/) line = line " " $i
			print line
			next
		}
		{ print }
	' "$work"
	cat "${work}.devspec"
} > "${work}.new"
mv "${work}.new" "$work"

	"$tooldir/bin/nbmakefs" -t ffs -B little \
	-o version=2,bsize=16384,fsize=2048 \
	-F "$work" -N "$os_source/etc" "$image" "$root"
cleanup
trap - EXIT HUP INT TERM
