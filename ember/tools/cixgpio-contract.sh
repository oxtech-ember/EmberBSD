#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD actual ACPI GPIO event runner, AI-assisted.
set -eu
[ "$#" -eq 0 ] || exit 2
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/cixgpio-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
# Only kernel includes are removed. Every production function is compiled intact.
ruby - "$src/sys/dev/acpi/acpi_event.c" "$work/acpi-event-source.inc" <<'RUBY'
text = File.read(ARGV[0]).lines.reject { |line| line.match?(/^#include /) }.join
File.write(ARGV[1],text)
RUBY
api=
if [ -f "$src/sys/dev/acpi/cixgpio_acpi.c" ]; then
    api=-DCIXGPIO_DRIVER_API
    ruby -e 'File.write(ARGV[1],File.readlines(ARGV[0]).reject { |line| line.start_with?("#include ") }.join)' \
      "$src/sys/dev/acpi/cixgpio_acpi.c" "$work/cixgpio-source.inc"
fi
if grep -q acpi_event_set_completion "$src/sys/dev/acpi/acpi_event.c"; then
    api="$api -DCIXGPIO_COMPLETION_API"
fi
# Build the fork's real ACPICA resource decoder. Only namespace/OS services are modeled.
# Only unchanged rsmisc.c can use explicit ASan-only instrumentation for its
# direct unaligned integer loads. Changed parser, driver and framework TUs
# always keep the complete caller-supplied CFLAGS, without suppression.
target=$(${CC:-cc} -dumpmachine)
case "$target" in
    *netbsd*) unconst=;;
    *) unconst='-D__UNCONST(a)=((void *)(a))';;
esac
set --
for source in "$src"/sys/external/bsd/acpica/dist/resources/*.c \
    "$src"/sys/external/bsd/acpica/dist/utilities/utalloc.c \
    "$src"/sys/external/bsd/acpica/dist/utilities/utdecode.c \
    "$src"/sys/external/bsd/acpica/dist/utilities/utresrc.c; do
    name=$(basename "$source" .c)
    case "$name" in rsdump*) continue;; esac
    case "$name" in
        rsmisc) parser_flags=${CIXGPIO_RSMISC_CFLAGS-${CFLAGS:-}};;
        *) parser_flags=${CFLAGS:-};;
    esac
    rename=
    if [ "$name" = rsxface ]; then rename=-DAcpiWalkResources=TestUnused_AcpiWalkResources; fi
    ${CC:-cc} -std=c11 $parser_flags -DACPI_APPLICATION \
        ${unconst:+"$unconst"} $rename -ffunction-sections -fdata-sections \
        -DACPI_MISALIGNMENT_NOT_SUPPORTED -isystem "$src/sys/external/bsd/acpica/dist/include" \
        -c "$source" -o "$work/$name.o"
    set -- "$@" "$work/$name.o"
done
case "$target" in *apple-darwin*) dead=-Wl,-dead_strip;; *) dead=-Wl,--gc-sections;; esac
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} $api -DACPI_MISALIGNMENT_NOT_SUPPORTED \
    -isystem "$src/sys/external/bsd/acpica/dist/include" -I"$work" \
    "$src/ember/tools/cixgpio-contract.c" "$@" $dead -o "$work/test"
if [ -n "${CIXGPIO_TEST_OUTPUT:-}" ]; then cp "$work/test" "$CIXGPIO_TEST_OUTPUT"; fi
if [ "${CIXGPIO_ANALYZE:-0}" = 1 ]; then
    ${CC:-cc} -std=c11 ${CFLAGS:-} -dM -E -x c /dev/null > "$work/gcc-predefined.h"
    cppcheck --quiet --check-level=exhaustive \
        --enable=warning,performance,portability --error-exitcode=1 \
        --include="$work/gcc-predefined.h" \
        --std=c11 --platform=unix64 -DACPI_APPLICATION \
        -DACPI_MISALIGNMENT_NOT_SUPPORTED -DCIXGPIO_DRIVER_API \
        -DCIXGPIO_COMPLETION_API -I"$work" -I"$src/ember/tools" \
        -I"$src/sys/external/bsd/acpica/dist/include" \
        "$src/ember/tools/cixgpio-contract.c"
    # Host static analysis only. GCC16 remains the target compiler.
    "${CSA:-clang}" --analyze -std=c11 -Wall -Wextra -Werror \
        -DACPI_MISALIGNMENT_NOT_SUPPORTED -DCIXGPIO_DRIVER_API \
        -DCIXGPIO_COMPLETION_API -isystem "$src/sys/external/bsd/acpica/dist/include" \
        -I"$work" "$src/ember/tools/cixgpio-contract.c" \
        -o "$work/cixgpio.plist" 2> "$work/csa.log"
    cat "$work/csa.log"
    test ! -s "$work/csa.log"
fi
if [ "${CIXGPIO_COMPILE_ONLY:-0}" = 1 ]; then exit 0; fi
if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/test"; else "$work/test"; fi
