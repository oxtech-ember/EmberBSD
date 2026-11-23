#!/bin/sh
# Install a fixture kernel on fixture boards and check the file it replaces.
# Optionally accept a plain ELF from an EmberBSD release as an additional input.
set -eu
[ "$#" -le 1 ] || { echo "usage: $0 [RELEASE_ELF]" >&2; exit 2; }
release_elf=${1:-}
if [ -n "$release_elf" ]; then [ -f "$release_elf" ]; fi
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/install-kernel.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
version() { printf 'NetBSD %s (%s) #0: Mon Nov 23 10:00:00 UTC 2026' "$1" "$2"; }
kernel() {
	# ELF64 little-endian ET_EXEC, EM_AARCH64. A 64-byte header and one
	# 56-byte PT_LOAD map this 256-byte file at 0x100000. Entry is 0x100080.
	printf '\177ELF\002\001\001\000\000\000\000\000\000\000\000\000'
	printf '\002\000\267\000\001\000\000\000'
	printf '\200\000\020\000\000\000\000\000'
	printf '\100\000\000\000\000\000\000\000'
	printf '\000\000\000\000\000\000\000\000\000\000\000\000'
	printf '\100\000\070\000\001\000\100\000\000\000\000\000'
	printf '\001\000\000\000\005\000\000\000'
	printf '\000\000\000\000\000\000\000\000'
	printf '\000\000\020\000\000\000\000\000'
	printf '\000\000\020\000\000\000\000\000'
	printf '\000\001\000\000\000\000\000\000'
	printf '\000\001\000\000\000\000\000\000'
	printf '\000\020\000\000\000\000\000\000'
	printf '\000\000\000\000\000\000\000\000'
	printf '\300\003\137\326\n'	# AArch64 ret at the entry address.
	banner="@(#)$(version "$@")"
	printf '%s\n' "$banner"
	dd if=/dev/zero bs=1 count="$((122 - ${#banner}))" 2>/dev/null
}
run() { PATH="$work/bin:$PATH" sh "$src/ember/install-kernel.sh" "$work/build" > "$work/log" 2>&1; }
fails() {
	before=$(mktemp -d "$work/before.XXXXXX")
	cp -Rp "$EMBER_ROOT" "$before/root"
	if run; then echo "FAIL: accepted ($1)" >&2; exit 1; fi
	grep -Eq "$1" "$work/log" || { cat "$work/log" >&2; exit 1; }
	# Includes ESP, FFS, every module and any unexpected staging file.
	diff -r "$before/root" "$EMBER_ROOT"
}
mkdir -p "$work/bin" "$work/build"
cat > "$work/bin/sysctl" <<'STUB'
#!/bin/sh
case "$2" in kern.version) cat "$EMBER_ROOT/running" ;; hw.disknames) cat "$EMBER_ROOT/disknames" ;; esac
STUB
printf '#!/bin/sh\ncat "$EMBER_ROOT/mount"\n' > "$work/bin/mount"
printf '#!/bin/sh\ncat "$EMBER_ROOT/gpt"\n' > "$work/bin/gpt"
printf '#!/bin/sh\ncat "$EMBER_ROOT/wedges"\n' > "$work/bin/dkctl"
printf '#!/bin/sh\necho evbarm\n' > "$work/bin/uname"
printf '#!/bin/sh\nexit 0\n' > "$work/bin/sync"
cat > "$work/bin/cp" <<'STUB'
#!/bin/sh
set -eu
corrupt=
dest=
for arg do
	if [ -n "${CORRUPT_COPY_SOURCE:-}" ] && [ "$arg" = "$CORRUPT_COPY_SOURCE" ]; then
		corrupt=yes
	fi
	dest=$arg
done
/bin/cp "$@"
if [ "$corrupt" = yes ]; then printf 'corrupt staged kernel\n' >> "$dest"; fi
STUB
chmod +x "$work/bin"/*
kernel 11.1 EMBER64 > "$work/build/netbsd-EMBER64"
printf module > "$work/build/rpi5button.kmod"

# board NAME: the development image layout, with netbsd on the ESP and on FFS.
board() {
	export EMBER_ROOT="$work/$1"
	mkdir -p "$EMBER_ROOT/boot"
	printf '/dev/dk1 on / type ffs (log, local)\n/dev/dk0 on /boot type msdos (local)\n' > "$EMBER_ROOT/mount"
	printf '      2048    262144      1  GPT part - EFI System\n    264192      1000      2  GPT part - NetBSD FFSv1/FFSv2\n' > "$EMBER_ROOT/gpt"
	printf 'dk0: EFI, 262144 blocks at 2048, type: msdos\ndk1: netbsd-root, 1000 blocks at 264192, type: ffs\n' > "$EMBER_ROOT/wedges"
	echo ld0 dk0 dk1 > "$EMBER_ROOT/disknames"
	kernel 11.0 OLD > "$EMBER_ROOT/netbsd"
	kernel 11.0 OLD > "$EMBER_ROOT/boot/netbsd"
	version 11.0 OLD > "$EMBER_ROOT/running"
	printf 'menu=Boot EmberBSD:boot netbsd root=NAME=netbsd-root console=com\r\n' > "$EMBER_ROOT/boot/boot.cfg"
}

# `boot netbsd` reads the FFS copy; the ESP copy stays.
board dev
run
cmp "$work/build/netbsd-EMBER64" "$EMBER_ROOT/netbsd"
grep -q 'NetBSD 11.0 (OLD)' "$EMBER_ROOT/netbsd.recovery"
grep -q 'NetBSD 11.0 (OLD)' "$EMBER_ROOT/boot/netbsd"
[ ! -e "$EMBER_ROOT/boot/netbsd.recovery" ]
cmp "$work/build/rpi5button.kmod" "$EMBER_ROOT/stand/evbarm/11.1/modules/rpi5button/rpi5button.kmod"
cat > "$work/expected" <<'CFG'
menu=Boot EmberBSD:boot netbsd root=NAME=netbsd-root console=com
menu=Recover previous kernel (single user):boot netbsd.recovery -s root=NAME=netbsd-root console=com
menu=Command prompt:prompt
default=1
timeout=5
CFG
cmp "$work/expected" "$EMBER_ROOT/boot/boot.cfg"

# `boot hd0b:netbsd` reads GPT entry 2.
board cm5
printf 'menu=Diagnostic:boot hd0a:netbsd.diag\nmenu=Boot:boot hd0b:netbsd -v\ndefault=2\n' > "$EMBER_ROOT/boot/boot.cfg"
run
cmp "$work/build/netbsd-EMBER64" "$EMBER_ROOT/netbsd"
grep -qx 'menu=Recover previous kernel (single user):boot hd0b:netbsd.recovery -s -v' "$EMBER_ROOT/boot/boot.cfg"

# A released ELF should also install through the same fixture path.
if [ -n "$release_elf" ]; then
	board released
	cp "$work/build/netbsd-EMBER64" "$work/minimal.elf"
	cp "$release_elf" "$work/build/netbsd-EMBER64"
	run
	cmp "$release_elf" "$EMBER_ROOT/netbsd"
	mv "$work/minimal.elf" "$work/build/netbsd-EMBER64"
fi

# Each invalid image retains a banner unless the corruption itself truncates it.
# Keep an existing module to detect accidental mutation on rejected installs.
cp "$work/build/netbsd-EMBER64" "$work/valid.elf"
bad_board() {
	board "$1"
	mkdir -p "$EMBER_ROOT/stand/evbarm/11.1/modules/rpi5button"
	printf 'installed sentinel module\n' > "$EMBER_ROOT/stand/evbarm/11.1/modules/rpi5button/rpi5button.kmod"
	cp "$work/valid.elf" "$work/build/netbsd-EMBER64"
}
bad_byte() {
	bad_board "$1"
	printf '%b' "$3" | dd of="$work/build/netbsd-EMBER64" bs=1 seek="$2" conv=notrunc 2>/dev/null
	fails 'ELF|kernel'
}
bad_byte format 0 '\000'
bad_byte class 4 '\001'
bad_byte endian 5 '\002'
bad_byte type 16 '\003'
bad_byte architecture 18 '\076'
bad_board truncated_header
dd if="$work/valid.elf" of="$work/build/netbsd-EMBER64" bs=1 count=60 2>/dev/null
fails 'ELF|kernel'
bad_board truncated_program_headers
dd if="$work/valid.elf" of="$work/build/netbsd-EMBER64" bs=1 count=100 2>/dev/null
fails 'ELF|kernel'
bad_board truncated_segment
dd if="$work/valid.elf" of="$work/build/netbsd-EMBER64" bs=1 count=240 2>/dev/null
fails 'ELF|kernel'
# Set p_offset to 1 while p_filesz remains equal to the file length.
bad_byte segment_out_of_file 72 '\001'

# The file bounds remain valid. Each case wraps one memory address range.
bad_board virtual_range_overflow
printf '\000\000\000\000\000\000\000\000' |
	dd of="$work/build/netbsd-EMBER64" bs=1 seek=88 conv=notrunc 2>/dev/null
printf '\377\377\377\377\377\377\377\377' |
	dd of="$work/build/netbsd-EMBER64" bs=1 seek=104 conv=notrunc 2>/dev/null
fails 'ELF|kernel'
bad_board physical_range_overflow
printf '\200\377\377\377\377\377\377\377' |
	dd of="$work/build/netbsd-EMBER64" bs=1 seek=88 conv=notrunc 2>/dev/null
fails 'ELF|kernel'

# A user ELF can include a plain version string but has no SCCS kernel marker.
bad_board plain_version
printf '    ' | dd of="$work/build/netbsd-EMBER64" bs=1 seek=133 conv=notrunc 2>/dev/null
fails 'version|SCCS|kernel'

# A memory-only segment tail, such as .bss, need not occupy file bytes.
bad_board bss
printf '\002' | dd of="$work/build/netbsd-EMBER64" bs=1 seek=105 conv=notrunc 2>/dev/null
run
cmp "$work/build/netbsd-EMBER64" "$EMBER_ROOT/netbsd"
bad_board corrupt_copy
export CORRUPT_COPY_SOURCE="$work/build/netbsd-EMBER64"
fails 'copy|staged|integrity|match'
unset CORRUPT_COPY_SOURCE

# Refusals change neither the kernel nor boot.cfg.
board twodisks; echo ld0 sd0 dk0 dk1 > "$EMBER_ROOT/disknames"; fails 'several disks'
board esp; printf 'menu=Boot:boot hd0a:netbsd\n' > "$EMBER_ROOT/boot/boot.cfg"; fails 'NetBSD FFS partition'
board unmounted; printf '/dev/dk0 on /boot type msdos (local)\n' > "$EMBER_ROOT/mount"; fails 'not mounted'
board other; version 11.0 OTHER > "$EMBER_ROOT/running"; fails 'is not the running kernel'
board recovery; printf 'menu=Boot:boot ./netbsd.recovery\n' > "$EMBER_ROOT/boot/boot.cfg"
kernel 11.0 OLD > "$EMBER_ROOT/netbsd.recovery"; fails 'other than netbsd.recovery'
echo 'PASS: install-kernel.sh replaces the file that efiboot boots and keeps recovery'
