#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
set -eu
[ "$#" -eq 0 ] || exit 2
analyze=${CIX_UART2_ANALYZE:-0}
compile_only=${CIX_UART2_COMPILE_ONLY:-0}
case "$analyze:$compile_only" in 0:0|0:1|1:0|1:1) ;; *) exit 2;; esac
if [ "$compile_only" = 1 ] && [ -z "${CIX_UART2_TEST_OUTPUT:-}" ]; then
    echo 'compile-only requires CIX_UART2_TEST_OUTPUT prefix' >&2
    exit 2
fi
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
# A console-only manual configuration must not add active device profiles.
ruby - "$src/sys/arch/evbarm/conf/EMBER64_CIX_UART2" \
    "$src/sys/arch/evbarm/conf/EMBER64_CIXVPU_UART2_BOOT_PROBE" \
    "$src/sys/arch/evbarm/conf/EMBER64_CIXVPU_RPC_UART2_BOOT_PROBE" \
    "$src/sys/arch/evbarm/conf/EMBER64_CIXSCMI_UART_RATE_QUERY" <<'RUBY'
ARGV.first(2).zip(%w[EMBER64 EMBER64_CIXVPU_BOOT_PROBE]).each do |path, parent|
  lines = File.readlines(path).map { |line| line.sub(/#.*/, '').strip }
  expected = ["include \"arch/evbarm/conf/#{parent}\"", 'options CIXP1_UART2_CONSOLE']
  abort 'manual profile differs' unless lines.reject(&:empty?) == expected
end
lines = File.readlines(ARGV.fetch(2)).map { |line| line.sub(/#.*/, '').strip }
expected = ['include "arch/evbarm/conf/EMBER64_CIXVPU_UART2_BOOT_PROBE"',
  'no cixvpu* at acpi?', 'cixvpu* at acpi? flags 0x8']
abort 'manual RPC profile differs' unless lines.reject(&:empty?) == expected
lines = File.readlines(ARGV.fetch(3)).map { |line| line.sub(/#.*/, '').strip }
expected = ['include "arch/evbarm/conf/EMBER64_CIXSCMI_QUERY"',
  'options CIXP1_UART2_CONSOLE', 'no cixscmi* at acpi?',
  'cixscmi* at acpi? flags 0x100']
abort 'manual UART rate profile differs' unless lines.reject(&:empty?) == expected
parent = File.join(File.dirname(ARGV.fetch(3)), 'EMBER64_CIXSCMI_QUERY')
lines = File.readlines(parent).map { |line| line.sub(/#.*/, '').strip }
expected = ['include "arch/evbarm/conf/EMBER64"', 'no cixvpu* at acpi?',
  'cixvpu* at acpi? flags 0x0', 'cixscmi* at acpi? flags 0x1']
abort 'manual UART rate parent differs' unless lines.reject(&:empty?) == expected
RUBY
work=$(mktemp -d "${TMPDIR:-/tmp}/cix-uart2.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
api=
if [ -f "$src/sys/arch/arm/acpi/cix_uart2_console.c" ]; then
    api=-DCIX_UART2_GATE
    ruby -e 'File.write(ARGV[1],File.readlines(ARGV[0]).reject { |s| s.start_with?("#include ") }.join)' \
        "$src/sys/arch/arm/acpi/cix_uart2_console.c" "$work/gate.inc"
fi
ruby - "$src/sys/arch/arm/acpi/acpi_platform.c" "$work/consumer.inc" <<'RUBY'
source = File.read(ARGV[0])
parts = []
guard = source.match(/^static bool\nacpi_platform_cix_uart2_spcr\([^\n]*\)\n\{.*?^\}/m)
if guard
  parts << "#if defined(CIXP1_UART2_CONSOLE) && NPLCOM > 0\n#{guard[0]}\n#endif"
end
%w[acpi_platform_attach_uart acpi_platform_startup].each do |name|
  body = source.match(/^static void\n#{name}\([^\n]*\)\n\{.*?^\}/m)
  abort "missing complete function #{name}" unless body
  parts << body[0]
end
File.write(ARGV[1], parts.join("\n"))
RUBY
result=0
for selected in 0 1; do
    option=
    [ "$selected" = 0 ] || option=-DCIXP1_UART2_CONSOLE
    # CFLAGS is an intentional compiler argument list.
    ${CC:-cc} -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} $api $option \
        -I"$work" "$src/ember/tools/cix-uart2-console-contract.c" -o "$work/test"
    if [ -n "${CIX_UART2_TEST_OUTPUT:-}" ]; then
        cp "$work/test" "$CIX_UART2_TEST_OUTPUT.$selected"
    fi
    if [ "$analyze" = 1 ]; then
        ${CC:-cc} -std=c11 -dM -E -x c /dev/null > "$work/predefined.h"
        cppcheck --quiet --check-level=exhaustive --enable=warning,performance,portability \
            --error-exitcode=1 --include="$work/predefined.h" --std=c11 --platform=unix64 \
            $api $option -I"$work" "$src/ember/tools/cix-uart2-console-contract.c"
        set --
        if [ "$(uname -s)" = Darwin ]; then set -- -isysroot "$SDKROOT"; fi
        if ! "${CSA:-clang}" --analyze -std=c11 -Wall -Wextra -Werror $api $option \
            -I"$work" "$@" "$src/ember/tools/cix-uart2-console-contract.c" \
            -o "$work/console.plist" 2> "$work/csa.log"; then
            cat "$work/csa.log" >&2
            exit 1
        fi
        cat "$work/csa.log"
        test ! -s "$work/csa.log"
    fi
    [ "$compile_only" = 0 ] || continue
    echo "manual option $selected"
    if [ -n "${TEST_RUNNER:-}" ]; then
        "$TEST_RUNNER" "$work/test" || result=1
    else
        "$work/test" || result=1
    fi
done
exit "$result"
