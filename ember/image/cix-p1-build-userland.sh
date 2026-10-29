#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Original EmberBSD reduced C userland build profile, AI-assisted.
# This produces a development cohort, not a complete base set or SD image.
set -eu
export LC_ALL=C
here=$(CDPATH= cd -- "${0%/*}" && pwd)
if [ "${1:-}" != --selected ]; then
    [ "$#" -eq 4 ] || { echo "usage: $0 SOURCE NEW_OUTPUT TOOLDIR TARGET_PREFIX" >&2; exit 2; }
    : "${EMBER_HOST_PREFIX:?Set EMBER_HOST_PREFIX to the selected native SDK prefix}"
    exec "$EMBER_HOST_PREFIX/bin/python3.15" -B "$here/cix-p1-native.py" "$@"
fi
shift
src=$1 out=$2 tools=$3 compiler=$4 sdk=$5 host_prefix=$6 host_family=$7 family=$8
shift 8
libedit=$1 llvm_source=$2 host_bfd=$3 jobs=$4 git=$5 ruby=$6 shasum=$7
shift 7
libarchive=$1 libevent=$2 byacc=$3 iscsi=$4 libpcap=$5 tcpwrappers=$6
shift 6
runtime_prefix=$1 resource=$2 unwinder=$3 compiler_resource=$4 version=$5 machine=$6 selected_builtins=$7
nativepython=$host_prefix/bin/python3.15
runner=$sdk/scripts/run-native-os.py
for path in "$src" "$out" "$tools" "$compiler" "$sdk" "$host_prefix" "$host_bfd"; do
    case "$path" in /*) ;; *) echo 'Absolute paths required.' >&2; exit 2;; esac
    case "$path" in *[!A-Za-z0-9_./-]*) echo 'Unsafe build path.' >&2; exit 2;; esac
done
[ -d "$src" ] && [ -f "$src/build.sh" ] || exit 2
[ ! -e "$out/BUILD-INPUTS.txt" ] || { echo "Output already exists." >&2; exit 2; }
# Base headers, libedit and sh consume the source prepared from the Ports recipe.
case "$libedit" in /*) ;; *) echo 'EMBER_LIBEDIT_SOURCE must name the prepared absolute source.' >&2; exit 2;; esac
case "$libedit" in *[!A-Za-z0-9_./-]*) echo 'Unsafe build path.' >&2; exit 2;; esac
[ -f "$libedit/libedit-source.json" ] && [ -f "$libedit/ports.distinfo" ] || {
    echo 'Prepared libedit source is incomplete.' >&2; exit 2
}
for tool in nbmkesdb nbmkcsmapper; do [ -x "$tools/bin/$tool" ]; done
if [ "$family" = clang ]; then
    cc=$compiler/bin/clang
    readelf=$compiler/bin/llvm-readelf
    runtime=$selected_builtins
    gcc_include=$compiler_resource/include
else
    cc=$compiler/bin/aarch64--netbsd-gcc
    readelf=$compiler/bin/aarch64--netbsd-readelf
    [ -x "$cc" ] && [ -x "$readelf" ] || exit 2
    version=$("$cc" -dumpfullversion)
    case "$version" in 16.*) ;; *) echo 'GCC16 is required.' >&2; exit 2;; esac
    machine=$("$cc" -dumpmachine)
    case "$machine" in aarch64--netbsd) ;; *) echo 'AArch64 NetBSD ABI is required.' >&2; exit 2;; esac
    runtime=$("$cc" -print-libgcc-file-name)
    unwinder=$("$cc" -print-file-name=libgcc_eh.a)
    [ -f "$unwinder" ] || unwinder=
    gcc_include=$("$cc" -print-file-name=include)
fi
[ -x "$cc" ] && [ -x "$readelf" ] && [ -f "$runtime" ] || exit 2
revision=$("$git" -C "$src" rev-parse HEAD)
"$git" -C "$src" diff --quiet
"$git" -C "$src" diff --cached --quiet
[ -z "$("$git" -C "$src" ls-files --others --exclude-standard)" ] || {
    echo 'Untracked source inputs are not accepted.' >&2; exit 2
}
case "$jobs" in ''|*[!0-9]*) exit 2;; esac
[ "$jobs" -ge 1 ] && [ "$jobs" -le 32 ] || exit 2
# Target headers come from this output; compiler headers come from its selected prefix.
CPPFLAGS="-nostdinc -isystem $out/dest/usr/include -isystem $gcc_include"
export CPPFLAGS
mkdir -p "$out/logs" "$out/dest"
printf '%s\n' "source=$revision" "compiler-family=$family" "compiler-version=$version" "target=$machine" \
    "profile=reduced-static-c-development" > "$out/BUILD-INPUTS.pending"
printf 'target-cppflags=%s\n' "$CPPFLAGS" >> "$out/BUILD-INPUTS.pending"
for input in "$src/build.sh" "$0" "$here/cix-p1-native.py" "$here/cix-p1-i18n.rb" "$cc" "$runtime" \
    "$tools/bin/nbmkesdb" "$tools/bin/nbmkcsmapper" "$libedit/libedit-source.json" \
    "$llvm_source/llvm-source.json" "$libarchive/emberbsd-prepared-source.json" \
    "$libevent/emberbsd-prepared-source.json" "$byacc/emberbsd-prepared-source.json" \
    "$iscsi/emberbsd-prepared-source.json" "$libpcap/emberbsd-prepared-source.json" \
    "$tcpwrappers/emberbsd-prepared-source.json"; do
    "$shasum" -a 256 "$input" >> "$out/BUILD-INPUTS.pending"
done
"$shasum" -a 256 "$0" "$here/cix-p1-native.py" "$here/cix-p1-i18n.rb" > "$out/PROFILE-SHA256.txt"
if [ -n "$unwinder" ]; then
    "$shasum" -a 256 "$unwinder" >> "$out/BUILD-INPUTS.pending"
else
    echo 'compiler-unwinder=not-provided' >> "$out/BUILD-INPUTS.pending"
fi
run()
{
    name=$1; shift
    if [ "$1" = "$wrapper" ]; then shift; set -- "$HOST_SH" "$wrapper" "$@"; fi
    if ! "$@" > "$out/logs/$name.log" 2>&1; then
        echo "Build failed: $out/logs/$name.log" >&2
        return 1
    fi
}
wrapper=$out/nbmake-evbarm
set -- -V "EMBER_LLVM_SOURCE=$llvm_source"
if [ "$family" = clang ]; then
    set -- "$@" -V "EMBER_LIBUNWIND_ARCHIVE=$unwinder" \
        -V EMBER_LLVM_RUNTIME_PHASE=bootstrap -V "EMBER_EXTERNAL_LLVM_RUNTIME=$runtime_prefix" \
        -V "EMBER_LLVM_RESOURCE_DIR=$resource" -V MKLIBCXX=no
fi
"$nativepython" -B "$runner" --host-prefix "$host_prefix" --compiler-family "$host_family" \
    --source "$src" --routing "$out/routing" --cwd "$src" --log "$out/logs/wrapper.log" \
    --build-sh -- -U -u -j"$jobs" -m evbarm -a aarch64 \
    -O "$out/obj" -D "$out/dest" -T "$tools" -w "$wrapper" \
    -V TOOLCHAIN_MISSING=yes -V "EXTERNAL_TOOLCHAIN=$compiler" \
    -V "EMBER_COMPILER_FAMILY=$family" -V "EMBER_HOST_BFD_CONFIG=$host_bfd" \
    -V "CPPFLAGS=$CPPFLAGS" -V "EMBER_LIBARCHIVE_SOURCE=$libarchive" \
    -V "EMBER_LIBEVENT_SOURCE=$libevent" -V "EMBER_BYACC_SOURCE=$byacc" \
    -V "EMBER_ISCSI_SOURCE=$iscsi" -V "EMBER_LIBPCAP_SOURCE=$libpcap" \
    -V "EMBER_TCP_WRAPPERS_SOURCE=$tcpwrappers" "$@" \
    -V MKDEBUG=no -V MKDEBUGLIB=no -V MKX11=no -V MKCXX=no -V MKCOMPAT=no \
    -V MKKMOD=no -V MKGCCCMDS=no -V MKGDB=no -V MKBINUTILS=no \
    -V MKATF=no -V MKKYUA=no -V MKMAN=no -V MKDOC=no -V MKINFO=no \
    -V MKHTML=no -V MKLINT=no -V MKPROFILE=no -V MKCVS=no \
    -V MKGROFF=no -V MKRUMP=no -V MKPIC=no -V MKPICINSTALL=no \
    -V MKPIE=no -V LDSTATIC=-static -V MKZFS=no -V MKDTRACE=no \
    -V MKPAM=no -V USE_PAM=no -V MKKERBEROS=no -V USE_KERBEROS=no \
    -V MKLDAP=no -V USE_LDAP=no -V MKYP=no -V USE_YP=no \
    -V MKSKEY=no -V USE_SKEY=no -V MKHESIOD=no -V USE_HESIOD=no \
    -V "EMBER_LIBEDIT_SOURCE=$libedit" -V "BUILDINFO=EmberBSD:$revision" makewrapper
run obj "$wrapper" -C "$src" obj
run directories "$wrapper" -C "$src" do-distrib-dirs
run includes "$wrapper" -C "$src" -j"$jobs" includes
"$shasum" -a 256 "$wrapper" >> "$out/BUILD-INPUTS.pending"
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
    crypto/external/apache2/openssl/lib/libssl external/bsd/blocklist/lib lib/libwrap \
    crypto/external/bsd/netpgp/libmj; do
    component "$part"
done
# libssh is a private object-tree dependency. It has no installed link library.
run openssh-lib-obj "$wrapper" -C "$src/crypto/external/bsd/openssh/lib" obj
run openssh-lib-build "$wrapper" -C "$src/crypto/external/bsd/openssh/lib" -j"$jobs" dependall
for part in bin/sh bin/cat bin/ls bin/cp bin/mv bin/rm bin/mkdir bin/chmod \
    bin/df bin/dd bin/echo bin/sleep bin/sync bin/hostname bin/pwd bin/kill \
    bin/date bin/ps bin/stty sbin/init sbin/mknod sbin/ifconfig sbin/route sbin/dmesg sbin/sysctl \
    sbin/mount sbin/mount_ffs sbin/mount_msdos sbin/mount_tmpfs sbin/mount_ptyfs sbin/umount sbin/reboot \
    sbin/shutdown sbin/rndctl sbin/ping sbin/fsck sbin/fsck_ffs sbin/gpt sbin/dkctl \
    usr.bin/stat usr.bin/wall usr.bin/cksum usr.bin/uname usr.bin/env \
    usr.bin/printf usr.bin/wc usr.bin/nc usr.bin/sed usr.bin/grep \
    usr.bin/head usr.bin/tail external/historical/nawk/bin usr.bin/sort \
    usr.bin/tr usr.bin/mktemp usr.bin/netstat libexec/getty usr.sbin/powerd usr.sbin/usbdevs \
    usr.sbin/envstat sbin/nvmectl sbin/wdogctl usr.sbin/acpitools/acpidump \
    external/bsd/wpa/bin/wpa_passphrase external/bsd/wpa/bin/wpa_supplicant \
    external/bsd/wpa/bin/wpa_cli crypto/external/bsd/openssh/bin/sshd \
    crypto/external/bsd/openssh/libexec/sshd-session crypto/external/bsd/openssh/libexec/sshd-auth \
    crypto/external/bsd/openssh/bin/ssh-keygen usr.bin/passwd usr.sbin/pwd_mkdb; do
    component "$part"
done
# Keep the reduced non-privsep profile, with both IPv4 and IPv6 clients.
component external/bsd/dhcpcd/sbin/dhcpcd SMALLPROG=1 SMALLPROG_INET6=1
component share/i18n/iconv
component share/i18n/esdb 'SUBDIR=ISO646 UTF' ISO646_PART=US UTF_PART=8
component share/i18n/csmapper SUBDIR=ISO646 PART_ISO646=US
printf '%s\n' 'i18n-recipes=iconv;esdb:ISO646-US,UTF-8;csmapper:ISO646-US' >> "$out/BUILD-INPUTS.pending"
"$ruby" "$here/cix-p1-i18n.rb" record "$out" "$revision"
printf 'i18n-inventory-sha256=%s\n' "$("$shasum" -a 256 "$out/I18N-INVENTORY.json" | awk '{print $1}')" \
    >> "$out/BUILD-INPUTS.pending"
run makedev "$wrapper" -C "$src/etc" MAKEDEV
for file in bin/sh bin/stty usr/sbin/usbdevs sbin/init sbin/mknod sbin/ifconfig sbin/dhcpcd sbin/fsck \
    sbin/fsck_ffs sbin/mount_msdos sbin/mount_tmpfs sbin/gpt sbin/dkctl sbin/shutdown sbin/rndctl \
    sbin/mount_ptyfs usr/sbin/sshd usr/libexec/sshd-session usr/libexec/sshd-auth \
    usr/bin/ssh-keygen usr/bin/passwd usr/sbin/pwd_mkdb \
    usr/bin/stat usr/bin/wall usr/sbin/wpa_supplicant usr/sbin/wpa_cli usr/sbin/powerd usr/libexec/getty \
    usr/sbin/envstat sbin/nvmectl sbin/wdogctl usr/sbin/acpidump; do
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
    "$shasum" -a 256 "$file" >> "$out/BINARIES-SHA256.txt"
done < "$out/programs.list"
[ -s "$out/BINARIES-SHA256.txt" ]
"$shasum" -a 256 -c "$out/PROFILE-SHA256.txt" >/dev/null
"$ruby" "$here/cix-p1-i18n.rb" verify "$out" "$revision" "$out/BUILD-INPUTS.pending" >/dev/null
"$git" -C "$src" diff --quiet
"$git" -C "$src" diff --cached --quiet
[ -z "$("$git" -C "$src" ls-files --others --exclude-standard)" ] || exit 1
[ "$("$git" -C "$src" rev-parse HEAD)" = "$revision" ]
mv "$out/BUILD-INPUTS.pending" "$out/BUILD-INPUTS.txt"
echo "Reduced static C cohort built from $revision. No image or media was written."
