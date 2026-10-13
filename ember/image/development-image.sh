#!/bin/sh
# Origin: EmberBSD (AI-assisted), assemble an offline AArch64 development image.
set -eu
export LC_ALL=C
[ "$#" = 11 ] || {
	echo "Usage: $0 SETS KERNEL KERNEL_REV FSCK FSCK_REV CTF_TOOLS CTF_REV PACKAGES PORTS_REV TOOLDIR NEW_WORK" >&2
	exit 2
}
sets=$1 kernel=$2 kernel_rev=$3 fsck=$4 fsck_rev=$5
ctf=$6 ctf_rev=$7 packages=$8 ports_rev=$9 tools=${10} work=${11}
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
for path in "$sets" "$kernel" "$fsck" "$ctf" "$packages" "$tools" "$work"; do
	case "$path" in /*) ;; *) echo 'Absolute paths required.' >&2; exit 2 ;; esac
	case "$path" in *[!A-Za-z0-9_./-]*) echo 'Unsafe path.' >&2; exit 2 ;; esac
done
for revision in "$kernel_rev" "$fsck_rev" "$ctf_rev" "$ports_rev"; do
	[ "${#revision}" = 40 ] || exit 2
	case "$revision" in *[!0-9a-f]*) exit 2 ;; esac
done
[ ! -e "$work" ] && [ ! -L "$work" ]
for tool in nbmakefs nbgpt; do [ -x "$tools/bin/$tool" ]; done
[ -f "$kernel" ] && [ -f "$fsck" ] && [ -d "$packages" ]
ctf_files='ctfconvert libdwarf.so.2.2 libdwarf.a libdwarf_p.a libdwarf_pic.a libdwarf.h dwarf.h'
for file in $ctf_files; do [ -f "$ctf/$file" ] && [ ! -L "$ctf/$file" ]; done
for set in base etc comp; do
	expected=$(sed -n "s/^SHA512 ($set.tar.xz) = //p" "$sets/SHA512")
	[ -n "$expected" ]
	[ "$(shasum -a 512 "$sets/$set.tar.xz" | awk '{print $1}')" = "$expected" ]
done
mkdir -p "$work/root" "$work/boot/EFI/BOOT"
root=$work/root
for set in base etc comp; do
	# Base manual names collide on common case-insensitive Mac volumes.
	tar -xpf "$sets/$set.tar.xz" -C "$root" --no-same-owner --exclude './usr/share/man'
done
# Use the accepted static OS fsck: the base-set version can select a UFS2 alternate
# at 64 KiB instead of makefs's primary at 8 KiB. Never ship that fallback.
# Break the rescue crunch hardlink before replacing this one entry point.
rm -f "$root/sbin/fsck_ffs" "$root/rescue/fsck_ffs"
cp "$fsck" "$root/sbin/fsck_ffs"
cp "$fsck" "$root/rescue/fsck_ffs"
chmod 555 "$root/sbin/fsck_ffs" "$root/rescue/fsck_ffs"
# Install the matching converter, development files and one current library.
mkdir -p "$root/usr/lib" "$root/usr/include"
rm -f "$root/usr/bin/ctfconvert" "$root/usr/include/libdwarf.h" "$root/usr/include/dwarf.h" \
    "$root/usr/lib/libdwarf.so" "$root/usr/lib/libdwarf.so.2" \
    "$root/usr/lib"/libdwarf.so.2.*
cp "$ctf/ctfconvert" "$root/usr/bin/ctfconvert"
chmod 555 "$root/usr/bin/ctfconvert"
cp "$ctf/libdwarf.h" "$root/usr/include/libdwarf.h"
chmod 444 "$root/usr/include/libdwarf.h"
cp "$ctf/dwarf.h" "$root/usr/include/dwarf.h"
chmod 444 "$root/usr/include/dwarf.h"
for file in libdwarf.so.2.2 libdwarf.a libdwarf_p.a libdwarf_pic.a; do
	rm -f "$root/usr/lib/$file"
	cp "$ctf/$file" "$root/usr/lib/$file"
	chmod 444 "$root/usr/lib/$file"
done
ln -s libdwarf.so.2.2 "$root/usr/lib/libdwarf.so.2"
ln -s libdwarf.so.2.2 "$root/usr/lib/libdwarf.so"
mkdir -p "$root/boot" "$root/proc" "$root/var/ember/packages" "$root/usr/libexec/ember"
bundle=$root/var/ember/packages
for archive in "$packages"/*.tgz; do
	[ -f "$archive" ] && [ ! -L "$archive" ]
	name=${archive##*/}
	case "$name" in *[!A-Za-z0-9_.+-]*) exit 2 ;; esac
	cp "$archive" "$bundle/$name"
done
(cd "$bundle"; shasum -a 256 ./*.tgz | sed 's,  ./,  ,') > "$bundle/packages.sha256"
printf '%s\n' "$ports_rev" > "$bundle/ports-revision"
cp "$here/install-development.sh" "$root/usr/libexec/ember/install-development"
cp "$here/../etc/rc.d/ember_development" "$root/etc/rc.d/ember_development"
chmod 555 "$root/usr/libexec/ember/install-development" "$root/etc/rc.d/ember_development"
sh "$here/install-development.sh" --verify "$bundle"
# Both base debugger entry points must stop selecting the old executable.
rm -f "$root/usr/bin/gdb" "$root/usr/bin/gdbtui"
ln -s /usr/pkg/bin/gdb "$root/usr/bin/gdb"
# The system compiler is the packaged GCC16; the base comp set ships GCC 12.
for tool in cc c++ cpp g++ gcc; do
	rm -f "$root/usr/bin/$tool"
	ln -s /usr/pkg/gcc16/bin/$tool "$root/usr/bin/$tool"
done
cp "$kernel" "$root/netbsd"
cp "$kernel" "$work/boot/netbsd"
cp "$root/usr/mdec/bootaa64.efi" "$work/boot/EFI/BOOT/bootaa64.efi"
cat > "$work/boot/boot.cfg" <<'EOF'
menu=Boot EmberBSD development:boot netbsd root=NAME=netbsd-root console=com
menu=Recovery (single user):boot netbsd -s root=NAME=netbsd-root console=com
default=1
timeout=10
EOF
cp "$work/boot/boot.cfg" "$work/boot/EFI/BOOT/boot.cfg"
cat > "$root/etc/fstab" <<'EOF'
NAME=netbsd-root / ffs rw,noatime 1 1
NAME=EFI /boot msdos rw 1 1
ptyfs /dev/pts ptyfs rw
procfs /proc procfs rw
tmpfs /var/shm tmpfs rw,-m1777,-sram%25
EOF
cat >> "$root/etc/rc.conf" <<'EOF'
rc_configured=YES
hostname=ember-development
no_swap=YES
savecore=NO
dhcpcd=YES
dhcpcd_flags="-b"
sshd=NO
wscons=NO
xdm=NO
ember_development=YES
EOF
{
	printf 'kernel-source=%s\nfsck-source=%s\nctf-source=%s\nports-source=%s\n' \
	    "$kernel_rev" "$fsck_rev" "$ctf_rev" "$ports_rev"
	printf 'SHA256 (netbsd) = %s\n' "$(shasum -a 256 "$kernel" | awk '{print $1}')"
	printf 'SHA256 (fsck_ffs) = %s\n' "$(shasum -a 256 "$fsck" | awk '{print $1}')"
	for file in $ctf_files; do
		printf 'SHA256 (%s) = %s\n' "$file" "$(shasum -a 256 "$ctf/$file" | awk '{print $1}')"
	done
	for set in base etc comp; do
		printf 'SHA512 (%s.tar.xz) = %s\n' "$set" \
		    "$(shasum -a 512 "$sets/$set.tar.xz" | awk '{print $1}')"
	done
	for script in development-image.sh install-development.sh; do
		printf 'SHA256 (%s) = %s\n' "$script" \
		    "$(shasum -a 256 "$here/$script" | awk '{print $1}')"
	done
	printf 'SHA256 (ember_development) = %s\n' \
	    "$(shasum -a 256 "$root/etc/rc.d/ember_development" | awk '{print $1}')"
	printf 'SHA256 (packages.sha256) = %s\n' \
	    "$(shasum -a 256 "$bundle/packages.sha256" | awk '{print $1}')"
} > "$work/inputs.txt"
cp "$work/inputs.txt" "$root/var/ember/image-inputs.txt"

# Use the distribution's target ownership and modes, never the extracting
# host's UID/GID. Remove replaced nodes before changing their types or data.
# Omit base manuals, whose names collide on case-insensitive build volumes.
awk '
$1 ~ /^\.\/usr\/share\/man(\/|$)/ { next }
$1 ~ /^\.\/usr\/lib\/libdwarf[._]/ { next }
$1 == "./usr/bin/ctfconvert" || $1 == "./usr/include/libdwarf.h" ||
    $1 == "./usr/include/dwarf.h" { next }
$1 == "./usr/bin/gdb" || $1 == "./usr/bin/gdbtui" ||
$1 == "./usr/bin/cc" || $1 == "./usr/bin/c++" || $1 == "./usr/bin/cpp" ||
$1 == "./usr/bin/g++" || $1 == "./usr/bin/gcc" ||
$1 == "./etc/fstab" || $1 == "./etc/rc.conf" ||
$1 == "./sbin/fsck_ffs" || $1 == "./rescue/fsck_ffs" { next }
{ print }
' "$root/etc/mtree/NetBSD.dist" "$root/etc/mtree/set.base" \
    "$root/etc/mtree/set.etc" "$root/etc/mtree/set.comp" > "$work/spec"
# Keep MAKEDEV's failure visible instead of hiding it behind a pipeline.
(cd "$root"; /bin/sh dev/MAKEDEV -s all) > "$work/devices.spec"
sed 's:^\./:./dev/:' "$work/devices.spec" >> "$work/spec"
cat >> "$work/spec" <<'EOF'
./netbsd type=file uname=root gname=wheel mode=0555
./sbin/fsck_ffs type=file uname=root gname=wheel mode=0555
./rescue/fsck_ffs type=file uname=root gname=wheel mode=0555
./boot type=dir uname=root gname=wheel mode=0755
./proc type=dir uname=root gname=wheel mode=0755
./etc/fstab type=file uname=root gname=wheel mode=0644
./etc/rc.conf type=file uname=root gname=wheel mode=0644
./etc/rc.d/ember_development type=file uname=root gname=wheel mode=0555
./usr/bin/gdb type=link uname=root gname=wheel mode=0755 link=/usr/pkg/bin/gdb
./usr/bin/cc type=link uname=root gname=wheel mode=0755 link=/usr/pkg/gcc16/bin/cc
./usr/bin/c++ type=link uname=root gname=wheel mode=0755 link=/usr/pkg/gcc16/bin/c++
./usr/bin/cpp type=link uname=root gname=wheel mode=0755 link=/usr/pkg/gcc16/bin/cpp
./usr/bin/g++ type=link uname=root gname=wheel mode=0755 link=/usr/pkg/gcc16/bin/g++
./usr/bin/gcc type=link uname=root gname=wheel mode=0755 link=/usr/pkg/gcc16/bin/gcc
./usr/bin/ctfconvert type=file uname=root gname=wheel mode=0555
./usr/include/libdwarf.h type=file uname=root gname=wheel mode=0444
./usr/include/dwarf.h type=file uname=root gname=wheel mode=0444
./usr/lib/libdwarf.so type=link uname=root gname=wheel mode=0755 link=libdwarf.so.2.2
./usr/lib/libdwarf.so.2 type=link uname=root gname=wheel mode=0755 link=libdwarf.so.2.2
./usr/lib/libdwarf.so.2.2 type=file uname=root gname=wheel mode=0444
./usr/lib/libdwarf.a type=file uname=root gname=wheel mode=0444
./usr/lib/libdwarf_p.a type=file uname=root gname=wheel mode=0444
./usr/lib/libdwarf_pic.a type=file uname=root gname=wheel mode=0444
./usr/libexec/ember type=dir uname=root gname=wheel mode=0755
./usr/libexec/ember/install-development type=file uname=root gname=wheel mode=0555
./var/ember type=dir uname=root gname=wheel mode=0755
./var/ember/image-inputs.txt type=file uname=root gname=wheel mode=0444
./var/ember/packages type=dir uname=root gname=wheel mode=0755
./var/ember/packages/packages.sha256 type=file uname=root gname=wheel mode=0444
./var/ember/packages/ports-revision type=file uname=root gname=wheel mode=0444
EOF
for archive in "$bundle"/*.tgz; do
	printf './var/ember/packages/%s type=file uname=root gname=wheel mode=0444\n' \
	    "${archive##*/}" >> "$work/spec"
done
# Traversal may need extra host permissions. makefs takes target modes from
# the untouched distribution entries and explicit overrides above.
# Change each parent before find descends: batching would visit 000 children
# before their ancestors become searchable.
find "$root" -type d -exec chmod u+rwx {} \;
"$tools/bin/nbmakefs" -Z -N "$root/etc" -t msdos -o volume_label=EMBERBOOT,fat_type=32 \
    -O 16m -s 80m "$work/disk.raw" "$work/boot"
"$tools/bin/nbmakefs" -Zrx -B le -N "$root/etc" -t ffs -O 96m -s 12188m \
    -o d=16384,f=8192,b=65536,v=2 -F "$work/spec" "$work/disk.raw" "$root"
"${QEMU_IMG:-qemu-img}" resize -f raw "$work/disk.raw" 12G
"$tools/bin/nbgpt" "$work/disk.raw" create
"$tools/bin/nbgpt" "$work/disk.raw" add -b 32768 -s 163840 -l EFI -t efi
"$tools/bin/nbgpt" "$work/disk.raw" set -a required -i 1
"$tools/bin/nbgpt" "$work/disk.raw" add -b 196608 -s 24961024 -l netbsd-root -t ffs
"${QEMU_IMG:-qemu-img}" convert -f raw -O qcow2 "$work/disk.raw" "$work/emberbsd-development.qcow2"
"${QEMU_IMG:-qemu-img}" check "$work/emberbsd-development.qcow2"
(cd "$work"; shasum -a 256 emberbsd-development.qcow2) > "$work/image.sha256"
echo 'Development image assembled; boot and accept its installed package set before release.'
