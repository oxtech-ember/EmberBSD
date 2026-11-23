#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Actual closed attachment functions; native service models only.
set -eu
[ "$#" -eq 0 ] || exit 2
case ${CIXGPIOINPUT_ANALYZE:-0} in 0|1) ;; *) exit 2 ;; esac
case ${CIXGPIOINPUT_COMPILE_ONLY:-0} in 0|1) ;; *) exit 2 ;; esac
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/cixgpio-input.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
# Import the exact native public GPIO structures and complete consumer bodies.
# Complete function bodies remain; only unused parameter declarations are annotated.
ruby - "$src" "$work" <<'IMPORT'
src,work=ARGV
%w[sys/sys/queue.h sys/sys/ioccom.h sys/sys/gpio.h sys/dev/gpio/gpiovar.h].each do |path|
    File.write(File.join(work,File.basename(path)),File.readlines(File.join(src,path)).reject { |line| line.start_with?('#include ') }.join)
end
body=File.read(ENV.fetch('CIXGPIOINPUT_GPIO_SOURCE',File.join(src,'sys/dev/gpio/gpio.c')))
struct=body[/^struct gpio_softc \{.*?^\};/m] or abort 'missing gpio_softc'
functions=%w[gpio_resume gpio_print gpio_pin_can_map gpio_pin_map gpio_npins gpio_pinbyname gpio_ioctl].map do |name|
    body[/^(?:static )?(?:int|bool)\n#{name}\(.*?^\}/m] or abort "missing #{name}"
end
# The native unused resume qualifier is annotated in its declaration only.
functions.map! do |text|
    text.sub('const pmf_qual_t *qual)', 'const pmf_qual_t *qual __attribute__((unused)))')
        .sub('const char *pnp)', 'const char *pnp __attribute__((unused)))')
end
File.write(File.join(work,'gpio-consumers.inc'),struct+"\n"+functions.join("\n")+"\n")
IMPORT
api=
source=${CIXGPIOINPUT_SOURCE:-"$src/sys/dev/acpi/cixgpio_inputs_acpi.c"}
if [ -f "$source" ]; then
    api=-DCIXGPIOINPUT_API
    ruby -e 'File.write(ARGV[1],File.readlines(ARGV[0]).reject { |s| s.start_with?("#include ") }.join)' \
        "$source" "$work/cixgpio-input-source.inc"
fi
if [ -n "${CIXGPIOINPUT_ARTIFACT_DIR:-}" ] && [ -n "$api" ]; then
    mkdir -p "$CIXGPIOINPUT_ARTIFACT_DIR"
    cp "$work/cixgpio-input-source.inc" "$work/gpio-consumers.inc" \
        "$work/queue.h" "$work/ioccom.h" "$work/gpio.h" "$work/gpiovar.h" \
        "$CIXGPIOINPUT_ARTIFACT_DIR/"
fi
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} $api \
    -isystem "$src/sys/external/bsd/acpica/dist/include" \
    -I"$work" "$src/ember/tools/cixgpio-input-contract.c" -o "$work/test"
if [ -n "${CIXGPIOINPUT_TEST_OUTPUT:-}" ]; then cp "$work/test" "$CIXGPIOINPUT_TEST_OUTPUT"; fi
if [ "${CIXGPIOINPUT_ANALYZE:-0}" = 1 ]; then
    ${CC:-cc} -std=c11 -dM -E -x c /dev/null > "$work/predefined.h"
    set --
    # Cppcheck's unix64 preset defines Linux. Match the actual Darwin compiler.
    if [ "$(uname -s)" = Darwin ]; then set -- -U__linux__; fi
    if ! cppcheck --quiet --check-level=exhaustive \
        --enable=warning,performance,portability --error-exitcode=1 \
        --include="$work/predefined.h" --std=c11 --platform=unix64 $api \
        -DACPI_APPLICATION "$@" -I"$work" -I"$src/sys/external/bsd/acpica/dist/include" \
        "$src/ember/tools/cixgpio-input-contract.c" 2> "$work/cppcheck.log"; then
        cat "$work/cppcheck.log"; exit 1
    fi
    cat "$work/cppcheck.log"
    test ! -s "$work/cppcheck.log"
    set --
    if [ "$(uname -s)" = Darwin ]; then set -- -isysroot "$SDKROOT"; fi
    if ! "${CSA:-clang}" --analyze -std=c11 -Wall -Wextra -Werror $api \
        -isystem "$src/sys/external/bsd/acpica/dist/include" -I"$work" "$@" \
        "$src/ember/tools/cixgpio-input-contract.c" \
        -o "$work/csa.plist" 2> "$work/csa.log"; then
        cat "$work/csa.log"; exit 1
    fi
    # The full native gpio_pin_can_map body has an inherited dead-store finding.
    # Compare against the same consumer/test TU without the new driver API.
    # Keep both raw diagnostics; reject any change instead of suppressing it.
    if ! "${CSA:-clang}" --analyze -std=c11 -Wall -Wextra -Werror \
        -isystem "$src/sys/external/bsd/acpica/dist/include" -I"$work" "$@" \
        "$src/ember/tools/cixgpio-input-contract.c" \
        -o "$work/csa-baseline.plist" 2> "$work/csa-baseline.log"; then
        cat "$work/csa-baseline.log"; exit 1
    fi
    cat "$work/csa.log"
    if ! cmp -s "$work/csa-baseline.log" "$work/csa.log"; then
        cat "$work/csa-baseline.log"; exit 1
    fi
    if [ -n "${CIXGPIOINPUT_ARTIFACT_DIR:-}" ]; then
        cp "$work/csa.log" "$work/csa-baseline.log" "$CIXGPIOINPUT_ARTIFACT_DIR/"
    fi
fi
if [ "${CIXGPIOINPUT_COMPILE_ONLY:-0}" = 1 ]; then exit 0; fi
if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/test"; else "$work/test"; fi
