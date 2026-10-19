#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD 800 Series PCH-S touchpad path contract, AI-assisted.
set -eu
[ "$#" -eq 0 ] || exit 2
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/igpio-mtps-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
fail() { echo "igpio-mtps-contract: $*" >&2; exit 1; }

# The touchpad I2C controller: Linux intel-lpss-pci.c lists these
# 800 Series PCH-S (MTP-S) LPSS I2C functions.
for pair in 0:7f4c 1:7f4d 2:7f4e 3:7f4f 4:7f7a 5:7f7b; do
    n=${pair%%:*} id=${pair#*:}
    grep -Eq "^#define	PCI_PRODUCT_INTEL_800SERIES_S_I2C_$n	0x$id	" \
        "$src/sys/dev/pci/pcidevs.h" || fail "pcidevs.h lacks I2C $n 0x$id"
    grep -Eq "^product INTEL 800SERIES_S_I2C_$n	0x$id	" \
        "$src/sys/dev/pci/pcidevs" || fail "pcidevs lacks I2C $n 0x$id"
    grep -Fq "{ .id = VIDDID(INTEL, 800SERIES_S_I2C_$n) }," \
        "$src/sys/arch/x86/pci/dwiic_pci.c" || fail "dwiic_pci lacks I2C $n"
done
# The GpioInt controller named by the touchpad's _CRS.
grep -Fq '{ .compat = "INTC1084" },' "$src/sys/dev/acpi/igpio_acpi.c" ||
    fail "igpio_acpi does not match INTC1084"
# ihidev must keep matching HID-over-I2C children by _CID PNP0C50.
grep -Fq '{ .compat = "PNP0C50" },' "$src/sys/dev/i2c/ihidev.c" ||
    fail "ihidev does not match PNP0C50"

# Only kernel includes are removed. Every production function is compiled intact.
strip() { grep -v '^#include ' "$1" > "$2"; }
strip "$src/sys/sys/gpio.h" "$work/sys-gpio.h"
strip "$src/sys/dev/gpio/gpiovar.h" "$work/gpiovar.h"
strip "$src/sys/dev/ic/igpiovar.h" "$work/igpiovar.h"
strip "$src/sys/dev/ic/igpioreg.h" "$work/igpioreg.h"
strip "$src/sys/dev/ic/igpio.c" "$work/igpio-source.inc"
strip "$src/sys/dev/acpi/igpio_acpi.c" "$work/igpio-acpi-source.inc"
case "$(${CC:-cc} -dumpmachine)" in
    *netbsd*) unconst=;;
    *) unconst='-D__UNCONST(a)=((void *)(uintptr_t)(a))';;
esac
# Upstream igpio.c has unused callback parameters and signed comparisons;
# the kernel build also uses -Wno-sign-compare. Nothing else is relaxed.
${CC:-cc} -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-sign-compare ${CFLAGS:-} \
    ${unconst:+"$unconst"} -DACPI_MISALIGNMENT_NOT_SUPPORTED \
    -isystem "$src/sys/external/bsd/acpica/dist/include" \
    -I"$work" "$src/ember/tools/igpio-mtps-contract.c" -o "$work/test"
if [ "${IGPIO_COMPILE_ONLY:-0}" = 1 ]; then exit 0; fi
if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/test"; else "$work/test"; fi
