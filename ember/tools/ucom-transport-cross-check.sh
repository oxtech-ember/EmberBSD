#!/bin/sh
# Origin: EmberBSD external-ucom kernel compatibility matrix, 2026-10-10.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
umask 077
fail() { echo "$*" >&2; exit 1; }
[ "$#" -eq 3 ] || fail 'usage: ucom-transport-cross-check.sh SOURCE KERNEL_BUILD ABSOLUTE_NEW_OUTPUT'
case "$1:$2:$3" in *[[:space:]]*) fail 'cross make paths must not contain whitespace' ;; esac
src=$(CDPATH= cd -- "$1" && pwd -P)
build=$(CDPATH= cd -- "$2" && pwd -P)
case "$3" in /*) ;; *) fail 'output must be absolute' ;; esac
parent=$(CDPATH= cd -- "$(dirname -- "$3")" && pwd -P)
name=$(basename -- "$3")
case "$name" in .|..) fail 'output must name a new directory' ;; esac
out=$parent/$name
case "$out/" in "$src/"*) fail 'output must be outside source tree' ;; esac
make=$build/tools/bin/nbmake-evbarm
kobj=$build/obj/kernels/EMBER64
[ -x "$make" ] && [ -f "$kobj/Makefile" ] || fail 'prepared EMBER64 cross build required'
cc=$("$make" -C "$kobj" S="$src/sys" -V '${CC}')
cppflags=$("$make" -C "$kobj" S="$src/sys" -V '${CPPFLAGS}')
cflags=$("$make" -C "$kobj" S="$src/sys" -V '${CFLAGS}')
case "$cppflags$cflags" in *\"*|*\'*) fail 'quoted make flags require the full kernel wrapper' ;; esac
mkdir "$out"
"$cc" --version > "$out/compiler.txt" 2>&1
"$cc" -dumpmachine > "$out/target.txt"
case "$(cat "$out/target.txt")" in aarch64*netbsd*) ;; *) fail 'AArch64 fork toolchain required' ;; esac
printf '%s\n' "$src" "$build" "$cc" "$cppflags" "$cflags" > "$out/inputs.txt"
if [ -f "$src/commit.txt" ]; then cp "$src/commit.txt" "$out/commit.txt"; fi

# Discover every existing method-table owner instead of trusting an old list.
for file in "$src"/sys/dev/usb/*.c; do
    if grep 'ucom_methods[[:space:]][^;]*=' "$file" >/dev/null; then
        basename "$file" .c
    fi
done | sort > "$out/parents.txt"
[ "$(wc -l < "$out/parents.txt" | tr -d ' ')" -eq 19 ] || fail 'method-table inventory changed; review matrix scope'
{ printf '%s\n' ucom ucom_transport_core ucom_transport_rx; cat "$out/parents.txt"; } > "$out/units.txt"
while IFS= read -r unit; do
    # Generated flags contain no quoting or whitespace-bearing paths. Split
    # them into arguments without eval; use the actual kernel compile flags.
    if ! (cd "$kobj"; set -f; "$cc" $cppflags $cflags -MD -MF "$out/$unit.d" \
        -c "$src/sys/dev/usb/$unit.c" -o "$out/$unit.o") \
        > "$out/$unit.log" 2>&1; then
        cat "$out/$unit.log" >&2
        fail "kernel object failed: $unit"
    fi
    file "$out/$unit.o" >> "$out/objects.txt"
    printf 'PASS _KERNEL %s\n' "$unit"
done < "$out/units.txt"
hash() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1"
    elif command -v shasum >/dev/null 2>&1; then shasum -a 256 "$1"
    else sha256 "$1"; fi
}
(
    cd "$kobj"
    awk '{ for (i = 1; i <= NF; i++) if ($i != "\\" && $i !~ /:$/) print $i }' "$out"/*.d |
        while IFS= read -r file; do [ ! -f "$file" ] || realpath "$file"; done |
        sort -u | while IFS= read -r file; do hash "$file"; done
) > "$out/kernel-dependencies.sha256"
for file in "$out"/*.o; do hash "$file"; done > "$out/outputs.sha256"
printf '22 kernel objects including 19 method-table owners (17 legacy and 2 MOXA); no link/native/USB claim\n' > "$out/scope.txt"
