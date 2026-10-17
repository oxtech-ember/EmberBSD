#!/bin/sh
# Origin: EmberBSD MOXA helpers kernel cross-compilation regression, 2026-10-09.
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
set -eu
umask 077
[ "$#" -eq 3 ] || { echo 'usage: moxa-core-cross-check.sh SOURCE KERNEL_BUILD ABSOLUTE_NEW_OUTPUT' >&2; exit 1; }
src=$(CDPATH= cd -- "$1" && pwd -P)
build=$(CDPATH= cd -- "$2" && pwd -P)
case "$3" in /*) ;; *) echo 'output must be absolute' >&2; exit 1 ;; esac
parent=$(CDPATH= cd -- "$(dirname -- "$3")" && pwd -P)
out=$parent/$(basename -- "$3")
case "$out/" in "$src/"*) echo 'output must be outside source tree' >&2; exit 1 ;; esac
make=$build/tools/bin/nbmake-evbarm
kobj=$build/obj/kernels/EMBER64
[ -x "$make" ] && [ -d "$kobj" ] || { echo 'prepared EMBER64 cross build required' >&2; exit 1; }
mkdir "$out"
cc=$("$make" -C "$src" -V '${CC}')
"$cc" --version > "$out/compiler.txt" 2>&1
target=$("$cc" -dumpmachine)
case "$target" in aarch64*netbsd*) ;; *) echo 'AArch64 fork toolchain required' >&2; exit 1 ;; esac
printf '%s\n' "$target" > "$out/target.txt"
printf '%s\n' "$src" "$build" "$cc" > "$out/inputs.txt"
for unit in umoxa_frame umoxa_g2_txq ucom_transport_core ucom_transport_rx; do
    "$cc" -std=c99 -O2 -Wall -Wextra -Werror -ffreestanding -nostdinc \
        -D_KERNEL -I "$kobj" -I "$src/sys" -MD -MF "$out/$unit.d" \
        -c "$src/sys/dev/usb/$unit.c" -o "$out/$unit.o" \
        > "$out/$unit.log" 2>&1 || { cat "$out/$unit.log" >&2; exit 1; }
    echo "PASS _KERNEL $unit"
done
file "$out/umoxa_g2_txq.o" "$out/umoxa_frame.o" "$out/ucom_transport_core.o" "$out/ucom_transport_rx.o" > "$out/objects.txt"
cat "$out/objects.txt"
printf '4 AArch64 kernel objects compiled; no kernel link/native/device check\n' > "$out/scope.txt"
