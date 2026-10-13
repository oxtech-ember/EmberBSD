#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted). Build the one-shot EFI display probe on the host.
set -eu
[ "$#" -eq 4 ] || [ "$#" -eq 5 ] || { echo "Usage: $0 OS_SOURCE NEW_OUTPUT CROSS_PREFIX SYSROOT [--test-chain]" >&2; exit 2; }
source=$(CDPATH= cd -- "$1" && pwd)
out=$2
cross=$(CDPATH= cd -- "$3" && pwd)
sysroot=$(CDPATH= cd -- "$4" && pwd)
owner=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
input=$owner/probe.c
if [ "$#" -eq 5 ]; then
    [ "$5" = --test-chain ] || exit 2
    input=$owner/chain-test.c
fi
cc=$cross/bin/aarch64--netbsd-gcc
ld=$cross/bin/aarch64--netbsd-ld
objcopy=$cross/bin/aarch64--netbsd-objcopy
[ "$("$cc" -dumpversion | cut -d. -f1)" = 16 ]
[ "$("$cc" -dumpmachine)" = aarch64--netbsd ]
efi=$source/sys/external/bsd/gnu-efi/dist
mkdir "$out"
out=$(CDPATH= cd -- "$out" && pwd)
# The intermediate ELF does not map ELF headers into the manually constructed
# PE image. Explicit PHDRS avoid GNU ld's implicit, unmapped PT_PHDR error.
sed -e '/ENTRY(_start)/a\
PHDRS { image PT_LOAD FLAGS(7); dynamic PT_DYNAMIC FLAGS(6); }
' "$efi/gnuefi/elf_aarch64_efi.lds" > "$out/efi.lds.tmp"
awk '
    { lines[NR]=$0 }
    END {
        for (i=1; i<=NR; i++) {
            if (lines[i+1] ~ /_etext/) sub(/}/,"} :image",lines[i]);
            if (lines[i] ~ /\.dynamic.*\*\(\.dynamic\)/) lines[i]=lines[i] " :image :dynamic";
            if (lines[i+2] ~ /\.rela.dyn/) sub(/}/,"} :image",lines[i]);
            print lines[i];
        }
    }
' "$out/efi.lds.tmp" > "$out/efi.lds"
rm "$out/efi.lds.tmp"
for item in "$input" "$efi/gnuefi/crt0-efi-aarch64.S" "$efi/gnuefi/reloc_aarch64.c"; do
    name=$(basename "$item")
    "$cc" --sysroot="$sysroot" -std=gnu11 -O2 -g -fpic -fshort-wchar \
        -ffreestanding -fno-stack-protector -fno-builtin -Wall -Wextra -Werror \
        -I"$efi/inc" -I"$efi/inc/aarch64" -I"$efi/inc/protocol" \
        -c "$item" -o "$out/$name.o"
done
"$ld" --no-dynamic-linker -nostdlib -T "$out/efi.lds" -Bsymbolic -shared \
    --defsym=EFI_SUBSYSTEM=0xa -o "$out/probe.so" "$out/"*.o
"$cross/bin/aarch64--netbsd-nm" -u "$out/probe.so" > "$out/undefined.txt"
[ ! -s "$out/undefined.txt" ]
"$objcopy" -j .text -j .sdata -j .data -j .dynamic -j .dynsym -j .rela \
    -j '.rel.*' -j '.rela.*' -j .reloc -O binary "$out/probe.so" "$out/BOOTAA64.EFI"
file "$out/BOOTAA64.EFI" | tee "$out/format.txt"
grep -q 'EFI application.*Aarch64' "$out/format.txt"
shasum -a 256 "$input" "$owner/rpi-firmware-protocol.h" "$0" \
    "$efi/gnuefi/crt0-efi-aarch64.S" "$efi/gnuefi/reloc_aarch64.c" \
    "$efi/gnuefi/elf_aarch64_efi.lds" "$out/BOOTAA64.EFI" > "$out/sha256.txt"
"$cc" --version > "$out/compiler.txt"
git -C "$source" rev-parse HEAD > "$out/source-revision.txt"
printf 'PASS: EFI application built; physical HDMI remains unverified\n'
