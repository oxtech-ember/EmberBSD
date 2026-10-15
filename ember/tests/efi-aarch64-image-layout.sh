#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; verify the real ARM64 EFI image's relocation origin.
set -eu
[ "$#" -eq 3 ] || {
    echo 'Usage: efi-aarch64-image-layout.sh EFI_ELF EFI_IMAGE TARGET_TOOL_PREFIX' >&2
    exit 2
}
elf=$1 image=$2 prefix=$3
[ -f "$elf" ] && [ -f "$image" ] || exit 2
symbols=$("${prefix}nm" -n "$elf")
base=$(printf '%s\n' "$symbols" | awk '$3 == "ImageBase" { print $1 }')
[ -n "$base" ] && [ "$((0x$base))" -eq 0 ] || {
    echo "FAIL: EFI ImageBase must be ELF address zero, found ${base:-missing}" >&2
    echo 'The startup passes its runtime ImageBase to the ELF RELATIVE relocator.' >&2
    exit 1
}
entry=$(printf '%s\n' "$symbols" | awk '$3 == "_start" { print $1 }')
pe=$("${prefix}objdump" -p "$image")
printf '%s\n' "$pe" | grep -q 'file format pei-aarch64-little'
pe_entry=$(printf '%s\n' "$pe" | awk '$1 == "AddressOfEntryPoint" { print $2 }')
[ -n "$entry" ] && [ -n "$pe_entry" ] && \
    [ "$((0x$entry))" -eq "$((0x$pe_entry))" ] || {
    echo 'FAIL: PE entry point differs from the linked startup address' >&2
    exit 1
}
magic=$(od -An -tx1 -N2 "$image" | tr -d ' \n')
[ "$magic" = 4d5a ] || { echo 'FAIL: image does not start at its MZ header' >&2; exit 1; }
printf '%s\n' 'PASS: ARM64 EFI MZ origin, zero ELF ImageBase and matching PE startup entry'
