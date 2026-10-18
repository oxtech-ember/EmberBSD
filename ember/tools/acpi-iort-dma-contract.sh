#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: original actual-source ACPI DMA runner, AI-assisted.
set -eu
[ "$#" -eq 0 ] || exit 2
analyze=${IORT_DMA_ANALYZE-0}
compile_only=${IORT_DMA_COMPILE_ONLY-0}
case "$analyze" in 0|1) ;; *) exit 2;; esac
case "$compile_only" in 0|1) ;; *) exit 2;; esac
if [ "$compile_only" = 1 ] && [ -z "${IORT_DMA_TEST_OUTPUT:-}" ]; then
    echo 'compile-only requires IORT_DMA_TEST_OUTPUT' >&2
    exit 2
fi
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/acpi-iort-dma.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
# Narrow native-TU boundary: preserve complete function bytes and copyrights.
# Do not replace a caller or duplicate its range/lookup algorithm in the fixture.
ruby - "$src" "$work" <<'RUBY'
require 'digest'
require 'json'
root, work = ARGV
manifest = []
parts = {
  'acpi-iort-native.inc' => ['sys/arch/arm/acpi/acpi_iort.c',
    %w[acpi_iort_named_component]],
  'acpi-dma-native.inc' => ['sys/arch/arm/acpi/acpi_machdep.c',
    %w[arm_acpi_dma_module arm_acpi_dma_init_ranges arm_acpi_dma_flags arm_acpi_dma64_tag]]
}
parts.each do |output, (path, names)|
  bytes = File.binread(File.join(root, path))
  lines = bytes.lines
  text = lines.take_while { |line| !line.start_with?('#include ') }.join
  if output == 'acpi-dma-native.inc'
    start = lines.index { |line| line.start_with?('static const char * const module_hid[]') }
    abort 'missing module_hid' unless start
    stop = (start...lines.size).find { |i| lines[i] == "};\n" }
    abort 'unterminated module_hid' unless stop
    text << lines[start..stop].join << "\n"
  end
  names.each do |name|
    starts = lines.each_index.select { |i| lines[i].start_with?("#{name}(") }
    abort "expected one #{name}" unless starts.size == 1 && starts[0] > 0
    start = starts[0] - 1
    stop = (starts[0]...lines.size).find { |i| lines[i] == "}\n" }
    abort "unterminated #{name}" unless stop
    body = lines[start..stop].join
    text << body << "\n"
    manifest << {source: path, function: name, first_line: start + 1,
      last_line: stop + 1, source_sha256: Digest::SHA256.hexdigest(bytes),
      function_sha256: Digest::SHA256.hexdigest(body)}
  end
  File.binwrite(File.join(work, output), text)
end
File.write(File.join(work, 'native-functions.json'), JSON.pretty_generate(manifest) + "\n")
RUBY
if [ -n "${IORT_DMA_ARTIFACT_DIR:-}" ]; then
    mkdir -p "$IORT_DMA_ARTIFACT_DIR"
    cp "$work/"*.inc "$work/native-functions.json" "$IORT_DMA_ARTIFACT_DIR/"
fi
# Match the native -Wall profile; the extracted native functions have unused
# parameters. CFLAGS is an intentional compiler argument list.
# shellcheck disable=SC2086
${CC:-cc} -std=c11 -Wall -Werror ${CFLAGS:-} \
    -isystem "$src/sys/external/bsd/acpica/dist/include" -I"$work" \
    "$src/ember/tools/acpi-iort-dma-contract.c" -o "$work/test"
if [ -n "${IORT_DMA_TEST_OUTPUT:-}" ]; then cp "$work/test" "$IORT_DMA_TEST_OUTPUT"; fi
if [ "$analyze" = 1 ]; then
    ${CC:-cc} -std=c11 -dM -E -x c /dev/null > "$work/predefined.h"
    if ! cppcheck --quiet --check-level=exhaustive \
        --enable=warning,performance,portability --error-exitcode=1 \
        --include="$work/predefined.h" --std=c11 --platform=unix64 \
        -DACPI_APPLICATION -I"$work" \
        -I"$src/sys/external/bsd/acpica/dist/include" \
        "$src/ember/tools/acpi-iort-dma-contract.c" 2> "$work/cppcheck.log"; then
        cat "$work/cppcheck.log" >&2
        exit 1
    fi
    cat "$work/cppcheck.log"
    test ! -s "$work/cppcheck.log"
    set --
    if [ "$(uname -s)" = Darwin ]; then set -- -isysroot "$SDKROOT"; fi
    if ! "${CSA:-clang}" --analyze -std=c11 -Wall -Werror \
        -isystem "$src/sys/external/bsd/acpica/dist/include" -I"$work" \
        "$@" "$src/ember/tools/acpi-iort-dma-contract.c" -o "$work/iort.plist" \
        2> "$work/csa.log"; then
        cat "$work/csa.log" >&2
        exit 1
    fi
    cat "$work/csa.log"
    test ! -s "$work/csa.log"
fi
[ "$compile_only" = 0 ] || exit 0
if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/test"; else "$work/test"; fi
