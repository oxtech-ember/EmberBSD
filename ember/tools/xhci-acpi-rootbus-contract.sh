#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Actual-source ACPI xHCI root-bus contract. AI-assisted.
set -eu
[ "$#" -eq 0 ] || exit 2
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
work=$(mktemp -d "${TMPDIR:-/tmp}/xhci-acpi-rootbus.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
case "${XHCI_ACPI_COMPILE_ONLY:-0}" in 0|1) ;; *) exit 2 ;; esac
if [ "${XHCI_ACPI_COMPILE_ONLY:-0}" = 1 ] && [ -z "${XHCI_ACPI_OUTPUT:-}" ]; then
    echo 'XHCI_ACPI_COMPILE_ONLY requires XHCI_ACPI_OUTPUT' >&2
    exit 2
fi
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
# Keep every frontend function body and its upstream attribution. Only native
# includes and the RCS identifier declaration cross the host-model boundary.
ruby - "${XHCI_ACPI_SOURCE:-$src/sys/dev/acpi/xhci_acpi.c}" "$src" "$work" <<'RUBY'
source, root, work = ARGV
text = File.read(source)
text = text.lines.reject { |line| line.start_with?('#include ', '__KERNEL_RCSID(') }.join
File.write(File.join(work, 'xhci-acpi-native.inc'), text)
File.write(File.join(work, 'queue-native.h'), File.read(File.join(root, 'sys/sys/queue.h')))
RUBY
# CFLAGS is a caller-supplied compiler argument list.
# shellcheck disable=SC2086
# Match the kernel's -Wall policy: its callback ABI has unused parameters.
"${CC:-cc}" -std=c11 -O2 -g -Wall -Werror ${CFLAGS:-} \
    -I"$work" "$src/ember/tools/xhci-acpi-rootbus-contract.c" -o "$work/contract"
if [ -n "${XHCI_ACPI_OUTPUT:-}" ]; then cp "$work/contract" "$XHCI_ACPI_OUTPUT"; fi
[ "${XHCI_ACPI_COMPILE_ONLY:-0}" = 0 ] || exit 0
if [ -n "${TEST_RUNNER:-}" ]; then
    "$TEST_RUNNER" "$work/contract"
else
    "$work/contract"
fi
