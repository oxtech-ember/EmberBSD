#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
[ "$#" -eq 0 ] || exit 2
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/cix-plcom.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then SDKROOT=$(xcrun --show-sdk-path); export SDKROOT; fi
mkdir -p "$work/dev/ic"
cp "$src/sys/dev/ic/scmi_smt_diag.h" "$work/dev/ic/scmi_smt_diag.h"
ruby -e 'File.write(ARGV[1],File.readlines(ARGV[0]).reject{|l|l.start_with?("#include ")}.join)' "$src/sys/dev/acpi/cixscmi_acpi.h" "$work/cixscmi-api.inc"
ruby -e 'File.write(ARGV[1],File.readlines(ARGV[0]).reject{|l|l.start_with?("#include ")}.join)' "${CIX_PLCOM_SOURCE:-$src/sys/arch/arm/acpi/plcom_acpi.c}" "$work/plcom-source.inc"
ruby -e 's=File.read(ARGV[0]);a=s.index("int\npl011comspeed(");b=s.index("#ifdef PLCOM_DEBUG",a);abort unless a&&b;File.write(ARGV[1],s[a...b])' "${CIX_PLCOM_DIVISOR_SOURCE:-$src/sys/arch/evbarm/dev/plcom.c}" "$work/divisor-source.inc"
ruby - "$src/sys/dev/acpi/acpi_resource.c" "$work/resource-parser.inc" <<'RESOURCE'
s=File.read(ARGV[0])
a=s.index("static void\nacpi_res_parse_irq(");b=s.index("static void\nacpi_res_parse_drq(",a)
c=s.index("\tcase ACPI_RESOURCE_TYPE_EXTENDED_IRQ:\n");e=s.index("\n\tcase ACPI_RESOURCE_TYPE_GENERIC_REGISTER:",c)
abort unless a&&b&&c&&e
File.write(ARGV[1],s[a...b]+"\nstatic void\nnative_extended_irq(struct native_walk_arg *arg, ACPI_RESOURCE *res)\n{\n\tconst struct native_walk_ops *ops = &native_ops;\n\tunsigned int i;\n\tswitch (res->Type) {\n"+s[c...e]+"\n\tdefault:\n\t\tbreak;\n\t}\n}\n")
RESOURCE
case ${CIX_PLCOM_ANALYZE:-0} in 0|1) ;; *) exit 2;; esac
ruby - "$src" <<'RUBYCONFIG'
root=ARGV.fetch(0)
lines=File.readlines(root+'/sys/arch/evbarm/conf/EMBER64_CIXUART_NATIVE').map{|l|l.sub(/#.*/,'').strip}.reject(&:empty?)
abort 'native UART profile differs' unless lines==['include "arch/evbarm/conf/EMBER64_CIXSCMI_UART_RATE_QUERY"','no plcom* at acpi?','plcom* at acpi? flags 0x1']
parent=File.read(root+'/sys/arch/evbarm/conf/EMBER64_CIXSCMI_UART_RATE_QUERY')
abort 'native UART2/SCMI profile missing' unless parent.include?('options CIXP1_UART2_CONSOLE')&&parent.include?('cixscmi* at acpi? flags 0x100')
base=File.read(root+'/sys/arch/evbarm/conf/EMBER64_CIXSCMI_QUERY')
abort 'native UART profile activates VPU' unless base.include?('cixvpu* at acpi? flags 0x0')
combined=File.readlines(root+'/sys/arch/evbarm/conf/EMBER64_CIX_PERIPHERALS').map{|l|l.sub(/#.*/,'').strip}.reject(&:empty?)
abort 'combined peripherals profile differs' unless combined==['include "arch/evbarm/conf/EMBER64_CIXUART_NATIVE"','cixgpioin* at acpi? flags 0x1']
RUBYCONFIG
if [ "${CIX_PLCOM_ANALYZE:-0}" = 1 ]; then ${CC:-cc} -std=c11 -dM -E -x c /dev/null > "$work/predefined.h"; fi
for count in 0 1; do

    ${CC:-cc} -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} -DNCIXSCMI_ACPI=$count -isystem "$src/sys/external/bsd/acpica/dist/include" -I"$work" "$src/ember/tools/cix-plcom-contract.c" -o "$work/test$count"
    if [ "${CIX_PLCOM_ANALYZE:-0}" = 1 ]; then
        if ! cppcheck --quiet --check-level=exhaustive --enable=warning,performance,portability --error-exitcode=1 --include="$work/predefined.h" --std=c11 --platform=unix64 -DACPI_APPLICATION -DNCIXSCMI_ACPI=$count -I"$work" -I"$src/sys/external/bsd/acpica/dist/include" "$src/ember/tools/cix-plcom-contract.c" 2> "$work/cppcheck$count.log"; then cat "$work/cppcheck$count.log"; exit 1; fi
        cat "$work/cppcheck$count.log"; test ! -s "$work/cppcheck$count.log"
        set --
        if [ "$(uname -s)" = Darwin ]; then set -- -isysroot "$SDKROOT"; fi
        if ! "${CSA:-clang}" --analyze -std=c11 -Wall -Wextra -Werror -DNCIXSCMI_ACPI=$count -isystem "$src/sys/external/bsd/acpica/dist/include" -I"$work" "$@" "$src/ember/tools/cix-plcom-contract.c" -o "$work/plcom$count.plist" 2> "$work/csa$count.log"; then cat "$work/csa$count.log"; exit 1; fi
        cat "$work/csa$count.log"; test ! -s "$work/csa$count.log"
    fi
    if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/test$count"; else "$work/test$count"; fi
done
