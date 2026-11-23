#!/bin/sh
# Install fixture kernels into fixture roots and check boot.cfg.
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/install-kernel.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
script=$src/ember/install-kernel.sh
kernel() { printf '\177ELF\002\001\001\000\000\000\000\000\000\000\000\000\002\000\267\000'; printf 'NetBSD %s (%s) #0\n' "$1" "$2"; }
fails() { if sh "$script" "$@" > "$work/log" 2>&1; then echo "FAIL: accepted $*" >&2; exit 1; fi; }
mkdir -p "$work/build"
kernel 11.1 EMBER64 > "$work/build/netbsd-EMBER64"
printf module > "$work/build/rpi5button.kmod"

# A board whose default entry names the root partition, as on the CM5.
export EMBER_ROOT="$work/cm5" EMBER_BOOT="$work/cm5/boot"
mkdir -p "$EMBER_BOOT"
kernel 11.0 OLD > "$EMBER_ROOT/netbsd"
cat > "$EMBER_BOOT/boot.cfg" <<'CFG'
menu=Old diagnostic:boot hd0a:netbsd.diag -v
menu=Boot EmberBSD:boot hd0b:netbsd -v root=NAME=netbsd-root # comment
consdev=com
default=1
default=2
timeout=5
CFG
cp "$EMBER_BOOT/boot.cfg" "$work/cm5.cfg"
sh "$script" "$work/build" > "$work/log"
cmp "$work/build/netbsd-EMBER64" "$EMBER_ROOT/netbsd"
grep -q 'NetBSD 11.0 (OLD)' "$EMBER_ROOT/netbsd.recovery"
cmp "$work/build/rpi5button.kmod" "$EMBER_ROOT/stand/$(uname -m)/11.1/modules/rpi5button/rpi5button.kmod"
cat > "$work/expected" <<'CFG'
consdev=com
menu=Boot EmberBSD:boot hd0b:netbsd -v root=NAME=netbsd-root
menu=Recover previous kernel (single user):boot hd0b:netbsd.recovery -s -v root=NAME=netbsd-root
menu=Command prompt:prompt
default=1
timeout=5
CFG
cmp "$work/expected" "$EMBER_BOOT/boot.cfg"
# Another kernel later keeps the first recovery kernel and the first backup.
kernel 11.1 SECOND > "$work/build/netbsd-EMBER64"
sh "$script" "$work/build" > "$work/log"
grep -q 'NetBSD 11.0 (OLD)' "$EMBER_ROOT/netbsd.recovery"
grep -q 'NetBSD 11.1 (SECOND)' "$EMBER_ROOT/netbsd"
cmp "$work/cm5.cfg" "$EMBER_BOOT/boot.cfg.before-install"
cmp "$work/expected" "$EMBER_BOOT/boot.cfg"
grep -q 'stay in single-user mode' "$work/log"

# A development image: the entry names no device; the kernel is on the loader volume.
export EMBER_ROOT="$work/dev" EMBER_BOOT="$work/dev/boot"
mkdir -p "$EMBER_BOOT/EFI/BOOT"
kernel 11.1 OLD > "$EMBER_BOOT/netbsd"
printf 'menu=Boot EmberBSD:boot netbsd root=NAME=netbsd-root console=com\r\ndefault=9\r\n' > "$EMBER_BOOT/boot.cfg"
cp "$EMBER_BOOT/boot.cfg" "$EMBER_BOOT/EFI/BOOT/boot.cfg"
sh "$script" "$work/build" > "$work/log"
grep -q 'NetBSD 11.1 (SECOND)' "$EMBER_BOOT/netbsd"
grep -q 'NetBSD 11.1 (OLD)' "$EMBER_BOOT/netbsd.recovery"
grep -qx 'menu=Recover previous kernel (single user):boot netbsd.recovery -s root=NAME=netbsd-root console=com' "$EMBER_BOOT/boot.cfg"
cmp "$EMBER_BOOT/boot.cfg" "$EMBER_BOOT/EFI/BOOT/boot.cfg"

# Refusals change nothing.
export EMBER_ROOT="$work/bad" EMBER_BOOT="$work/bad/boot"
mkdir -p "$EMBER_BOOT"
printf 'menu=Boot EmberBSD:boot hd0b:netbsd\n' > "$EMBER_BOOT/boot.cfg"
fails "$work/build"
grep -q 'which is absent' "$work/log"
printf 'menu=Boot:userconf disable foo;boot hd0b:netbsd\n' > "$EMBER_BOOT/boot.cfg"
kernel 11.0 OLD > "$EMBER_ROOT/netbsd"
fails "$work/build"
grep -q 'runs several commands' "$work/log"
printf 'MZ native image' > "$work/build/netbsd-EMBER64"
fails "$work/build"
grep -q 'is not an AArch64 ELF64 kernel' "$work/log"
[ ! -e "$EMBER_ROOT/netbsd.recovery" ] && [ ! -e "$EMBER_BOOT/boot.cfg.before-install" ]
echo 'PASS: install-kernel.sh installs on root and loader volumes, keeps recovery and refuses before writing'
