#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/aarch64-reboot-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
# cpu_reboot is the last function. Keep its entire body and native flag values.
{
    printf 'void\n'
    sed -n '/^cpu_reboot(int howto, char \*bootstr)$/,$p' \
        "$src/sys/arch/aarch64/aarch64/aarch64_reboot.c"
} > "$work/cpu_reboot.inc"
cp "$src/sys/sys/reboot.h" "$work/reboot.h"
cp "$src/ember/tools/aarch64-reboot-contract.c" "$work/contract.c"
# CFLAGS and TEST_RUNNER follow the existing host contract convention.
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -Wall -Werror ${CFLAGS:-} -I"$work" \
    "$work/contract.c" -o "$work/contract"
if [ -n "${REBOOT_EVIDENCE:-}" ]; then
    mkdir "$REBOOT_EVIDENCE"
    cp "$work/contract.c" "$work/cpu_reboot.inc" "$work/reboot.h" \
        "$work/contract" "$REBOOT_EVIDENCE/"
fi
if [ -n "${TEST_RUNNER:-}" ]; then
    "$TEST_RUNNER" "$work/contract"
else
    "$work/contract"
fi
