#!/bin/sh
# Check the production VHT builder and bounded association decoder.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/aicwf-vht.XXXXXXXX")
if [ "${AICWF_VHT_KEEP:-0}" = 1 ]; then
    printf 'Test outputs: %s\n' "$work"
else
    trap 'rm -rf "$work"' EXIT HUP INT TERM
fi
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${AICWF_VHT_CFLAGS:-} \
    -I"$src/sys/dev/sdmmc" "$src/ember/tools/aicwf-vht-test.c" \
    -o "$work/check"
"$work/check"
