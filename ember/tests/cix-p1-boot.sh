#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: actual ARM efiboot CIX profile consumer, AI-assisted.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
out=${1:?usage: cix-p1-boot.sh OUTPUT_DIRECTORY}
mkdir -p "$out"
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
# Retain each source's notices, then extract complete unchanged bodies.
extract() {
    file=$1
    name=$2
    type=$3
    awk '/^#/ { exit } { print }' "$file"
    printf '%s\n' "$type"
    awk -v name="$name" '$0 ~ "^" name "\\(" { copying=1 }
        copying { print } copying && /^}/ { exit }' "$file"
}
cp "$root/sys/lib/libsa/bootcfg.h" "$out/bootcfg-native.h"
cp "$root/sys/sys/queue.h" "$out/queue-native.h"
awk '/^#define (MENUFORMAT_|DEFAULT_)/ { print }
    /^#ifndef DEFAULT_TIMEOUT/ { print }
    /^#endif/ && !ended { print; ended=1 }' \
    "$root/sys/lib/libsa/bootcfg.c" > "$out/bootcfg-defines.inc"
awk '/^static char (initrd_path|netbsd_args)\[/ { print }' \
    "$root/sys/stand/efiboot/boot.c" > "$out/globals-native.inc"
extract "$root/sys/stand/efiboot/prompt.c" gettrailer 'char *' > "$out/commands-native.inc"
extract "$root/sys/stand/efiboot/boot.c" set_initrd_path 'int' >> "$out/commands-native.inc"
extract "$root/sys/stand/efiboot/boot.c" command_acpi 'void' >> "$out/commands-native.inc"
extract "$root/sys/stand/efiboot/boot.c" command_boot 'void' >> "$out/commands-native.inc"
extract "$root/sys/stand/efiboot/boot.c" command_initrd 'void' >> "$out/commands-native.inc"
extract "$root/sys/stand/efiboot/boot.c" command_userconf 'void' >> "$out/commands-native.inc"
printf 'const struct boot_command commands[] = {\n' > "$out/table-native.inc"
awk '/^[[:space:]]*{ "(acpi|boot|initrd|userconf)",/ { print }' \
    "$root/sys/stand/efiboot/boot.c" >> "$out/table-native.inc"
printf '{ NULL, NULL, NULL } };\n' >> "$out/table-native.inc"
extract "$root/sys/stand/efiboot/prompt.c" docommand 'void' > "$out/dispatch-native.inc"
printf 'struct bootcfg_def bootcfg_info;\n' > "$out/config-native.inc"
extract "$root/sys/lib/libsa/bootcfg.c" perform_bootcfg 'int' >> "$out/config-native.inc"
extract "$root/sys/stand/efiboot/bootmenu.c" do_bootcfg_command 'static void' >> "$out/config-native.inc"
extract "$root/sys/stand/efiboot/bootmenu.c" parsebootconf 'int' >> "$out/config-native.inc"
extract "$root/sys/stand/efiboot/bootmenu.c" getchoicefrominput 'static int' > "$out/menu-native.inc"
extract "$root/sys/stand/efiboot/bootmenu.c" docommandchoice 'static void' >> "$out/menu-native.inc"
sed '/^#include "efiboot.h"/d; /^#include <sys\/queue.h>/d' \
    "$root/sys/stand/efiboot/userconf.c" > "$out/userconf-native.inc"
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${TEST_CFLAGS:-} \
    -I"$out" "$root/ember/tests/cix-p1-boot.c" -o "$out/cix-p1-boot"
cfg=${CIX_BOOT_CONFIG:-"$root/ember/boot/cix-p1-persistent.cfg"}
if [ -n "${TEST_RUNNER:-}" ]; then
    "$TEST_RUNNER" "$out/cix-p1-boot" "$cfg" normal
    "$TEST_RUNNER" "$out/cix-p1-boot" "$cfg" recovery
else
    "$out/cix-p1-boot" "$cfg" normal
    "$out/cix-p1-boot" "$cfg" recovery
fi
