#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: actual-source RTL8822C packet-H2C DMA consumer contract.
set -eu
[ "$#" -eq 0 ] || exit 2
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/rtw88-fw-init-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work/include/sys" "$work/include/dev"
ln -s "$src/sys/dev/ic" "$work/include/dev/ic"
for header in sys/param.h sys/systm.h; do
    printf '#include <errno.h>\n#include "rtw88-test-kernel.h"\n' > "$work/include/$header"
done
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${RTW88_TEST_CFLAGS:-} \
    -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" \
    "$tools/rtw88-fw-init-contract.c" \
    "${RTW88_FW_INIT_SOURCE:-$src/sys/dev/ic/rtw88_fw_init.c}" \
    "$src/sys/dev/ic/rtw88_hw.c" -o "$work/test"
if [ -n "${TEST_RUNNER:-}" ]; then
    "$TEST_RUNNER" "$work/test" "$tools/rtw88-fw-init-contract.vectors"
else
    "$work/test" "$tools/rtw88-fw-init-contract.vectors"
fi
