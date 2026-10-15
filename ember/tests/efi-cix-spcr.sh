#!/bin/sh
# Origin: EmberBSD bounded CIX SPCR command contracts, 2026-10-15.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
out=${1:?usage: efi-cix-spcr.sh OUTPUT_DIRECTORY}
mkdir -p "$out"
# Compile unchanged production function bodies with only firmware services
# replaced. No parser or command logic is copied into the test implementation.
sed '/^#include "efiboot.h"/d; /^#include "smbios.h"/d' \
    "$root/sys/stand/efiboot/efiacpi.c" > "$out/efiacpi-body.c"
{ printf 'void\n'; sed -n '/^command_acpi(char \*arg)/,/^}/p' \
    "$root/sys/stand/efiboot/boot.c"; } > "$out/command-body.c"
extra=
if [ -f "$root/sys/stand/efiboot/cix_spcr.c" ]; then
    extra=${CIX_SOURCE:-"$root/sys/stand/efiboot/cix_spcr.c"}
fi
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${TEST_CFLAGS:-} \
    -DEFIBOOT_ACPI -I"$out" -I"$root/sys/stand/efiboot" \
    -I"$root/sys/external/bsd/gnu-efi/dist/inc" \
    -I"$root/sys/external/bsd/gnu-efi/dist/inc/aarch64" \
    "$root/ember/tests/efi-cix-spcr.c" $extra ${TEST_LDFLAGS:-} -o "$out/efi-cix-spcr"
"$out/efi-cix-spcr"
