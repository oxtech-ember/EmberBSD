#!/bin/sh
# Install a fixture kernel on fixture boards and check the file it replaces.
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/install-kernel.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
version() { printf 'NetBSD %s (%s) #0: Mon Nov 23 10:00:00 UTC 2026' "$1" "$2"; }
kernel() { printf '\177ELF'; version "$@"; echo; }
run() { PATH="$work/bin:$PATH" sh "$src/ember/install-kernel.sh" "$work/build" > "$work/log" 2>&1; }
fails() {
	cp "$EMBER_ROOT/boot/boot.cfg" "$work/before.cfg"
	if run; then echo "FAIL: accepted ($1)" >&2; exit 1; fi
	grep -q "$1" "$work/log" || { cat "$work/log" >&2; exit 1; }
	cmp -s "$work/before.cfg" "$EMBER_ROOT/boot/boot.cfg"
	grep -q 'NetBSD 11.0 (OLD)' "$EMBER_ROOT/netbsd"
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

# Refusals change neither the kernel nor boot.cfg.
board twodisks; echo ld0 sd0 dk0 dk1 > "$EMBER_ROOT/disknames"; fails 'several disks'
board esp; printf 'menu=Boot:boot hd0a:netbsd\n' > "$EMBER_ROOT/boot/boot.cfg"; fails 'NetBSD FFS partition'
board unmounted; printf '/dev/dk0 on /boot type msdos (local)\n' > "$EMBER_ROOT/mount"; fails 'not mounted'
board other; version 11.0 OTHER > "$EMBER_ROOT/running"; fails 'is not the running kernel'
board recovery; printf 'menu=Boot:boot ./netbsd.recovery\n' > "$EMBER_ROOT/boot/boot.cfg"
kernel 11.0 OLD > "$EMBER_ROOT/netbsd.recovery"; fails 'other than netbsd.recovery'
echo 'PASS: install-kernel.sh replaces the file that efiboot boots and keeps recovery'
