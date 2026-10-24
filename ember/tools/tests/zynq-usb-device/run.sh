#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$dir/../../../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/ember-zynqudc.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
for file in sys/dev/usb/usbperiph.h sys/arch/arm/xilinx/zynq_usb_devicereg.h sys/arch/arm/xilinx/zynq_usb_devicevar.h sys/arch/arm/xilinx/zynq_usb_device_core.c; do
 sed '/^#include/d' "$root/$file" >> "$work/driver.inc"
done
awk '
 /^struct zynqudc_softc \{/ {copy=1}
 /^extern struct cfdriver/ {copy=0}
 copy {print}
 /^static (int|bool|void)$/ || /^static void __dead$/ || /^static struct zynqudc_softc \*/ {pending=$0;next}
 /^zynqudc_ulpi\(/ || /^zynqudc_irq\(/ {tail=1}
 /^zynqudc_attach\(/ {tail=0}
 tail {if (pending != "") {print pending;pending=""} print}
' "$root/sys/arch/arm/xilinx/zynq_usb_device.c" > "$work/cdev.inc"
${CC:?select GCC16 explicitly} -std=c11 -Wall -Wextra -Werror -O1 -g -pthread ${CFLAGS:-} -I"$work" "$dir/test.c" -o "$work/test"
"$work/test"
if [ "${ANALYZE:-no}" = yes ]; then
 "${CPPCHECK:?select cppcheck}" --enable=warning,style,performance,portability --error-exitcode=98 --inline-suppr --suppress=missingIncludeSystem -I"$work" "$dir/test.c"
 "$CC" -std=c11 -Wall -Wextra -Werror -O1 -g -fanalyzer ${CFLAGS:-} -I"$work" "$dir/test.c" -o "$work/analyzed"
 "$work/analyzed"
fi
