#!/bin/sh
# Origin: EmberBSD UPort 1150 configured kernel-object check, 2026-10-13.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
umask 077
fail() { echo "$*" >&2; exit 1; }
[ "$#" -eq 3 ] || fail 'usage: umoxa1150-cross-check.sh SOURCE PREPARED_BUILD NEW_OUTPUT'
case "$1:$2:$3" in *[[:space:]]*) fail 'cross make paths must not contain whitespace' ;; esac
src=$(CDPATH= cd -- "$1" && pwd -P)
build=$(CDPATH= cd -- "$2" && pwd -P)
case "$3" in /*) ;; *) fail 'output must be absolute' ;; esac
parent=$(CDPATH= cd -- "$(dirname -- "$3")" && pwd -P)
out=$parent/$(basename -- "$3")
case "$out/" in "$src/"*) fail 'output must be outside source tree' ;; esac
[ -x "$build/tools/bin/nbconfig" ] && [ -x "$build/tools/bin/nbmake-evbarm" ] || fail 'prepared fork tools required'
mkdir "$out"
mkdir "$out/kernel"
"$build/tools/bin/nbconfig" -s "$src/sys" -b "$out/kernel" \
    "$src/sys/arch/evbarm/conf/EMBER64" > "$out/config.log" 2>&1 || { cat "$out/config.log" >&2; exit 1; }
make=$build/tools/bin/nbmake-evbarm
"$make" -C "$out/kernel" NETBSDSRCDIR="$src" MAKEOBJDIR="$out/kernel" \
    -m "$src/share/mk" umoxa1150.o umoxa1150_protocol.o \
    > "$out/build.log" 2>&1 || { cat "$out/build.log" >&2; exit 1; }
cc=$("$make" -C "$out/kernel" -V '${CC}')
"$cc" --version > "$out/compiler.txt"
"$cc" -dumpmachine > "$out/target.txt"
case "$(cat "$out/target.txt")" in aarch64*netbsd*) ;; *) fail 'AArch64 fork compiler required' ;; esac
file "$out/kernel/umoxa1150.o" "$out/kernel/umoxa1150_protocol.o" > "$out/objects.txt"
cat "$out/objects.txt"
printf '%s\n' "$src" "$build" > "$out/inputs.txt"
printf 'Fresh EMBER64 config and two configured AArch64 objects; no kernel link/boot/USB claim\n' > "$out/scope.txt"
