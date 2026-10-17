#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Original EmberBSD reduced C userland build profile, AI-assisted.
# This produces a development cohort, not a complete base set or SD image.
set -eu
export LC_ALL=C
[ "$#" -eq 4 ] || {
    echo "usage: $0 SOURCE NEW_OUTPUT TOOLDIR GCC16_PREFIX" >&2
    exit 2
}
src=$1 out=$2 tools=$3 compiler=$4
here=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
for path in "$src" "$out" "$tools" "$compiler"; do
    case "$path" in /*) ;; *) echo 'Absolute paths required.' >&2; exit 2;; esac
    case "$path" in *[!A-Za-z0-9_./-]*) echo 'Unsafe build path.' >&2; exit 2;; esac
done
[ -d "$src" ] && [ -f "$src/build.sh" ] || exit 2
[ ! -e "$out" ] && [ ! -L "$out" ] || exit 2
[ -x "$tools/bin/nbmake" ]
for tool in nbmkesdb nbmkcsmapper; do [ -x "$tools/bin/$tool" ]; done
command -v ruby >/dev/null
cc=$compiler/bin/aarch64--netbsd-gcc
readelf=$compiler/bin/aarch64--netbsd-readelf
[ -x "$cc" ] && [ -x "$readelf" ] || exit 2
version=$("$cc" -dumpfullversion)
case "$version" in 16.*) ;; *) echo 'GCC16 is required.' >&2; exit 2;; esac
machine=$("$cc" -dumpmachine)
case "$machine" in aarch64--netbsd) ;; *) echo 'AArch64 NetBSD ABI is required.' >&2; exit 2;; esac
runtime=$("$cc" -print-libgcc-file-name)
unwinder=$("$cc" -print-file-name=libgcc_eh.a)
[ -f "$runtime" ] || exit 2
[ -f "$unwinder" ] || unwinder=
revision=$(git -C "$src" rev-parse HEAD)
git -C "$src" diff --quiet
git -C "$src" diff --cached --quiet
[ -z "$(git -C "$src" ls-files --others --exclude-standard)" ] || {
    echo 'Untracked source inputs are not accepted.' >&2; exit 2
}
jobs=${BUILD_JOBS:-2}
case "$jobs" in ''|*[!0-9]*) exit 2;; esac
[ "$jobs" -ge 1 ] && [ "$jobs" -le 32 ] || exit 2
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
HOST_CC=${HOST_CC:-gcc-16}; HOST_CXX=${HOST_CXX:-g++-16}
export HOST_CC HOST_CXX
MAKECONF=/dev/null; export MAKECONF
# A compiler's sys-include symlink can ignore --sysroot. Fence target headers.
gcc_include=$("$cc" -print-file-name=include)
# include-fixed can also contain copies of headers from an older target root.
CPPFLAGS="-nostdinc -isystem $out/dest/usr/include -isystem $gcc_include"
export CPPFLAGS
mkdir -p "$out/logs" "$out/dest"
printf '%s\n' "source=$revision" "gcc=$version" "target=$machine" \
    "profile=reduced-static-c-development" > "$out/BUILD-INPUTS.pending"
printf 'target-cppflags=%s\n' "$CPPFLAGS" >> "$out/BUILD-INPUTS.pending"
for input in "$src/build.sh" "$0" "$here/cix-p1-i18n.rb" "$cc" "$runtime" \
    "$tools/bin/nbmkesdb" "$tools/bin/nbmkcsmapper"; do
    shasum -a 256 "$input" >> "$out/BUILD-INPUTS.pending"
done
shasum -a 256 "$0" "$here/cix-p1-i18n.rb" > "$out/PROFILE-SHA256.txt"
if [ -n "$unwinder" ]; then
    shasum -a 256 "$unwinder" >> "$out/BUILD-INPUTS.pending"
else
    echo 'compiler-unwinder=not-provided' >> "$out/BUILD-INPUTS.pending"
fi
run()
{
    name=$1; shift
    if ! "$@" > "$out/logs/$name.log" 2>&1; then
        echo "Build failed: $out/logs/$name.log" >&2
        return 1
    fi
}
wrapper=$out/nbmake-evbarm
run wrapper sh "$src/build.sh" -U -u -j"$jobs" -m evbarm -a aarch64 \
    -O "$out/obj" -D "$out/dest" -T "$tools" -w "$wrapper" \
    -V TOOLCHAIN_MISSING=yes -V "EXTERNAL_TOOLCHAIN=$compiler" \
    -V MKDEBUG=no -V MKDEBUGLIB=no -V MKX11=no -V MKCXX=no \
    -V MKKMOD=no -V MKGCCCMDS=no -V MKGDB=no -V MKBINUTILS=no \
    -V MKATF=no -V MKKYUA=no -V MKMAN=no -V MKDOC=no -V MKINFO=no \
    -V MKHTML=no -V MKLINT=no -V MKPROFILE=no -V MKCVS=no \
    -V MKGROFF=no -V MKRUMP=no -V MKPIC=no -V MKPICINSTALL=no \
    -V MKPIE=no -V LDSTATIC=-static -V MKZFS=no -V MKDTRACE=no \
    -V "BUILDINFO=EmberBSD:$revision" makewrapper
run obj "$wrapper" -C "$src" obj
run directories "$wrapper" -C "$src" do-distrib-dirs
run includes "$wrapper" -C "$src" -j"$jobs" includes
shasum -a 256 "$wrapper" >> "$out/BUILD-INPUTS.pending"
component()
{
    part=$1; shift
    label=$(printf '%s' "$part" | tr / _)
    run "$label-obj" "$wrapper" -C "$src/$part" "$@" obj
    run "$label-build" "$wrapper" -C "$src/$part" -j"$jobs" "$@" dependall
    run "$label-install" "$wrapper" -C "$src/$part" "$@" install
}
# Rebuild the selected libraries. Old archives used another SSP/header profile.
for part in lib/csu lib/libc lib/libterminfo lib/libedit lib/libutil \
    lib/libcrypt lib/libprop lib/libkvm lib/libm lib/libz lib/libbz2 lib/libipsec \
    external/bsd/libpcap/lib crypto/external/apache2/openssl/lib/libcommon \
    crypto/external/apache2/openssl/lib/libcrypto \
    crypto/external/apache2/openssl/lib/libdes \
    crypto/external/apache2/openssl/lib/libssl; do
    component "$part"
done
for part in bin/sh bin/cat bin/ls bin/cp bin/mv bin/rm bin/mkdir bin/chmod \
    bin/df bin/dd bin/echo bin/sleep bin/sync bin/hostname bin/pwd bin/kill \
    bin/date bin/ps bin/stty sbin/init sbin/mknod sbin/ifconfig sbin/route sbin/dmesg sbin/sysctl \
    sbin/mount sbin/mount_ffs sbin/mount_msdos sbin/mount_tmpfs sbin/umount sbin/reboot \
    sbin/shutdown sbin/rndctl sbin/ping sbin/fsck sbin/fsck_ffs sbin/gpt sbin/dkctl \
    usr.bin/stat usr.bin/wall usr.bin/cksum usr.bin/uname usr.bin/env \
    usr.bin/printf usr.bin/wc usr.bin/nc usr.bin/sed usr.bin/grep \
    usr.bin/head usr.bin/tail external/historical/nawk/bin usr.bin/sort \
    usr.bin/tr usr.bin/mktemp usr.bin/netstat libexec/getty usr.sbin/powerd usr.sbin/usbdevs \
    external/bsd/wpa/bin/wpa_passphrase external/bsd/wpa/bin/wpa_supplicant \
    external/bsd/wpa/bin/wpa_cli; do
    component "$part"
done
component external/bsd/dhcpcd/sbin/dhcpcd SMALLPROG=1
component share/i18n/iconv
component share/i18n/esdb 'SUBDIR=ISO646 UTF' ISO646_PART=US UTF_PART=8
component share/i18n/csmapper SUBDIR=ISO646 PART_ISO646=US
printf '%s\n' 'i18n-recipes=iconv;esdb:ISO646-US,UTF-8;csmapper:ISO646-US' >> "$out/BUILD-INPUTS.pending"
ruby "$here/cix-p1-i18n.rb" record "$out" "$revision"
printf 'i18n-inventory-sha256=%s\n' "$(shasum -a 256 "$out/I18N-INVENTORY.json" | awk '{print $1}')" \
    >> "$out/BUILD-INPUTS.pending"
run makedev "$wrapper" -C "$src/etc" MAKEDEV
for file in bin/sh bin/stty usr/sbin/usbdevs sbin/init sbin/mknod sbin/ifconfig sbin/dhcpcd sbin/fsck \
    sbin/fsck_ffs sbin/mount_msdos sbin/mount_tmpfs sbin/gpt sbin/dkctl sbin/shutdown sbin/rndctl \
    usr/bin/stat usr/bin/wall usr/sbin/wpa_supplicant usr/sbin/wpa_cli usr/sbin/powerd usr/libexec/getty; do
    [ -f "$out/dest/$file" ] || {
        echo "Missing required program: $file" >&2; exit 1
    }
done
# Inspect every installed ELF candidate. This does not execute target binaries.
find "$out/dest/bin" "$out/dest/sbin" "$out/dest/usr/bin" \
    "$out/dest/usr/sbin" "$out/dest/usr/libexec" -type f \
    > "$out/programs.list"
: > "$out/BINARIES-SHA256.txt"
while IFS= read -r file; do
    "$readelf" -h "$file" > "$out/elf-header.tmp"
    grep -q 'Class:.*ELF64' "$out/elf-header.tmp"
    grep -q 'Data:.*little endian' "$out/elf-header.tmp"
    grep -q 'Machine:.*AArch64' "$out/elf-header.tmp"
    grep -q 'Type:.*EXEC' "$out/elf-header.tmp"
    "$readelf" -l "$file" > "$out/elf-segments.tmp"
    if grep -E 'INTERP|DYNAMIC' "$out/elf-segments.tmp"; then
        echo "Nonstatic executable: $file" >&2; exit 1
    fi
    shasum -a 256 "$file" >> "$out/BINARIES-SHA256.txt"
done < "$out/programs.list"
[ -s "$out/BINARIES-SHA256.txt" ]
shasum -a 256 -c "$out/PROFILE-SHA256.txt" >/dev/null
ruby "$here/cix-p1-i18n.rb" verify "$out" "$revision" "$out/BUILD-INPUTS.pending" >/dev/null
git -C "$src" diff --quiet
git -C "$src" diff --cached --quiet
[ -z "$(git -C "$src" ls-files --others --exclude-standard)" ] || exit 1
[ "$(git -C "$src" rev-parse HEAD)" = "$revision" ]
mv "$out/BUILD-INPUTS.pending" "$out/BUILD-INPUTS.txt"
echo "Reduced static C cohort built from $revision. No image or media was written."
