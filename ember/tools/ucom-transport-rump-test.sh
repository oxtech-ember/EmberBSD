#!/bin/sh
# Origin: EmberBSD real-ucom native software contract runner, 2026-10-10.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
umask 077
LC_ALL=C
export LC_ALL
fail() { echo "$*" >&2; exit 1; }
[ "$#" -eq 2 ] || fail 'usage: ucom-transport-rump-test.sh SOURCE ABSOLUTE_NEW_OUTPUT'
case "$1:$2" in *[[:space:]]*) fail 'native make paths must not contain whitespace' ;; esac
case "$1:$2" in /*:/*) ;; *) fail 'absolute paths required' ;; esac
[ "$(uname -s)" = NetBSD ] || fail 'matching NetBSD 11 native runtime required'
case "$(uname -r)" in 11.*) ;; *) fail 'matching NetBSD 11 runtime required' ;; esac
src=$(CDPATH= cd -- "$1" && pwd -P)
parent=$(CDPATH= cd -- "$(dirname -- "$2")" && pwd -P)
name=$(basename -- "$2")
case "$name" in .|..) fail 'output must name a new directory' ;; esac
out=$parent/$name
case "$out/" in "$src/"*) fail 'output must be outside source tree' ;; esac
mkdir "$out"
mkdir "$out/lib"
unset MAKEOBJDIRPREFIX LD_LIBRARY_PATH LD_PRELOAD
{
    uname -srvm
    gcc --version
    printf 'source=%s\n' "$src"
    [ ! -f "$src/commit.txt" ] || cat "$src/commit.txt"
} > "$out/environment.log"
fixture=$src/tests/dev/usb/ucom_transport
if ! MAKEOBJDIR="$out/lib" make -C "$fixture" -j2 USETOOLS=no \
    NETBSDSRCDIR="$src" MKPROFILE=no MKDEBUGLIB=no MKLINT=no MKMAN=no \
    dependall > "$out/build.log" 2>&1; then
    tail -60 "$out/build.log" >&2
    fail 'private rump component build failed'
fi
if ! gcc -std=gnu99 -O2 -Wall -Wextra -Werror -pthread -I "$fixture" \
    "$fixture/t_ucom_transport.c" -L "$out/lib" -Wl,-rpath,"$out/lib" \
    -Wl,--whole-archive -lrumpdev_ucom_mock -Wl,--no-whole-archive \
    -lrumpkern_tty -lrumpdev -lrumpvfs -lrumpvfs_nofifofs -lrump -lrumpuser -lpthread \
    -o "$out/test" > "$out/test-build.log" 2>&1; then
    cat "$out/test-build.log" >&2
    fail 'native test link failed'
fi
ldd "$out/test" > "$out/runtime.ldd"
awk '$2 == "=>" { print $3 }' "$out/runtime.ldd" | sort -u |
    while IFS= read -r file; do sha256 "$file"; done > "$out/runtime.sha256"
(
    cd "$out/lib"
    awk '{ for (i = 1; i <= NF; i++) if ($i != "\\" && $i !~ /:$/) print $i }' ./*.d |
        while IFS= read -r file; do [ ! -f "$file" ] || realpath "$file"; done |
        sort -u | while IFS= read -r file; do sha256 "$file"; done
) > "$out/kernel-dependencies.sha256"
sha256 "$out/test" "$out/lib/librumpdev_ucom_mock.a" > "$out/outputs.sha256"
if ! RUMP_NCPU=4 "$out/test" > "$out/run.log" 2>&1; then
    cat "$out/run.log" >&2
    fail 'native ucom tests failed'
fi
cat "$out/run.log"
printf 'real ucom/TTY in native rump with test-only USB; no hardware or complete OS claim\n' > "$out/scope.txt"
