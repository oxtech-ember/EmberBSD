#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/iwx-sae.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
: "${CC:?select the native compiler with CC}"
flags=${IWX_TEST_CFLAGS:-${SAE_TEST_CFLAGS:-${CFLAGS:-}}}
CFLAGS="$flags" sh "$src/ember/tools/iwx-width-contract.sh"
CFLAGS="$flags" sh "$src/ember/tools/iwx-ht-contract.sh"
"${PYTHON:-python3}" "$src/ember/tools/iwx-sae-contract.py" \
    --cc "$CC" --cflags="$flags" --build-dir "$work/management"
"${PYTHON:-python3}" "$src/ember/tools/iwx-sae-firmware-contract.py" \
    --cc "$CC" --cflags="$flags" --build-dir "$work/firmware"
"${PYTHON:-python3}" "$src/ember/tools/iwx-sae-bounds-contract.py" \
    --cc "$CC" --cflags="$flags" --build-dir "$work/bounds"
if [ "${IWX_ANALYZE:-${SAE_ANALYZE:-0}}" = 1 ]; then
    "${PYTHON:-python3}" "$src/ember/tools/net80211-sae-analysis.py" \
        "$work/management/compile_commands.json" \
        "$work/firmware/compile_commands.json" \
        "$work/bounds/compile_commands.json"
fi
