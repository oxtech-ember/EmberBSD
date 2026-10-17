#!/bin/sh
# Origin: EmberBSD external-ucom target-runtime cross build, 2026-10-10.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
umask 077
LC_ALL=C
export LC_ALL
fail() { echo "$*" >&2; exit 1; }
[ "$#" -eq 4 ] || fail 'usage: ucom-transport-rump-cross-build.sh SOURCE KERNEL_BUILD TARGET_RUNTIME_SYSROOT ABSOLUTE_NEW_OUTPUT'
case "$1:$2:$3:$4" in *[[:space:]]*) fail 'cross make paths must not contain whitespace' ;; esac
for arg do
    case "$arg" in /*) ;; *) fail 'absolute paths required' ;; esac
done
src=$(CDPATH= cd -- "$1" && pwd -P)
build=$(CDPATH= cd -- "$2" && pwd -P)
runtime=$(CDPATH= cd -- "$3" && pwd -P)
parent=$(CDPATH= cd -- "$(dirname -- "$4")" && pwd -P)
name=$(basename -- "$4")
case "$name" in .|..) fail 'output must name a new directory' ;; esac
out=$parent/$name
case "$out/" in "$src/"*|"$runtime/"*|"$build/"*) fail 'output must be outside input trees' ;; esac
make=$build/tools/bin/nbmake-evbarm
cc=$build/tools/bin/aarch64--netbsd-gcc
[ -x "$make" ] && [ -x "$cc" ] || fail 'prepared AArch64 fork cross tools required'
[ -f "$runtime/usr/include/rump/rump_syscalls.h" ] &&
    [ -f "$runtime/usr/lib/librumpkern_tty.so" ] &&
    [ -f "$runtime/usr/lib/crt0.o" ] || fail 'matching target headers, rump libraries and C runtime required'
case $("$cc" -dumpmachine) in aarch64*netbsd*) ;; *) fail 'AArch64 target required' ;; esac
command -v shasum >/dev/null || fail 'host shasum required for receipts'
mkdir "$out"
mkdir "$out/lib"
unset MAKEOBJDIRPREFIX LD_LIBRARY_PATH LD_PRELOAD
{
    uname -srvm
    "$cc" --version
    printf 'source=%s\nruntime=%s\n' "$src" "$runtime"
    [ ! -f "$src/commit.txt" ] || cat "$src/commit.txt"
} > "$out/environment.log"
fixture=$src/tests/dev/usb/ucom_transport
actual_obj=$("$make" -C "$fixture" MAKEOBJDIR="$out/lib" -V .OBJDIR)
[ "$actual_obj" = "$out/lib" ] || fail 'cross make did not honor private object directory'
if ! "$make" -C "$fixture" -j2 MAKEOBJDIR="$out/lib" \
    NETBSDSRCDIR="$src" DESTDIR="$runtime" MKPROFILE=no MKDEBUGLIB=no \
    MKLINT=no MKMAN=no dependall > "$out/build.log" 2>&1; then
    tail -60 "$out/build.log" >&2
    fail 'private rump component cross build failed'
fi
if ! "$cc" --sysroot="$runtime" -std=gnu99 -O2 -Wall -Wextra -Werror \
    -pthread -I "$fixture" -MD -MF "$out/test.d" "$fixture/t_ucom_transport.c" \
    -Wl,-t,--whole-archive "$out/lib/librumpdev_ucom_mock.a" \
    -Wl,--no-whole-archive -lrumpkern_tty -lrumpdev -lrumpvfs \
    -lrumpvfs_nofifofs -lrump -lrumpuser -lpthread -o "$out/test" \
    > "$out/link.log" 2>&1; then
    tail -60 "$out/link.log" >&2
    fail 'target test link failed'
fi
(
    cd "$out/lib"
    awk '{ for (i = 1; i <= NF; i++) if ($i != "\\" && $i !~ /:$/) print $i }' ./*.d "$out/test.d" |
        while IFS= read -r path; do [ ! -f "$path" ] || realpath "$path"; done |
        sort -u | while IFS= read -r path; do shasum -a 256 "$path"; done
) > "$out/dependencies.sha256"
while IFS= read -r path; do
    [ ! -f "$path" ] || shasum -a 256 "$path"
done < "$out/link.log" > "$out/link-inputs.sha256"
file "$out/test" > "$out/objects.txt"
shasum -a 256 "$out/test" "$out/lib/librumpdev_ucom_mock.a" > "$out/outputs.sha256"
printf 'cross-built software fixture; execute on the matching target runtime; no complete OS or hardware claim\n' > "$out/scope.txt"
cat "$out/objects.txt"
printf 'PASS private rump component and target test cross build\n'
