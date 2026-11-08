#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Exercise native SAE ownership, BSD supplicant and driver link-loss lifetime.
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/net80211-sae.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
: "${CC:?select the native compiler with CC}"
flags=${SAE_TEST_CFLAGS:-${CFLAGS:-}}
"${PYTHON:-python3}" "$src/sys/net80211/tests/native-sae.py" \
    --cc "$CC" --cflags="$flags" --build-dir "$work/native"
"${PYTHON:-python3}" "$src/ember/tools/bsd-sae-contract.py" \
    --cc "$CC" --cflags="$flags" --build-dir "$work/bsd"
mkdir "$work/rtw89"
"${PYTHON:-python3}" "$src/ember/tools/rtw89-link-loss-contract.py" \
    --source-root "$src" --work-dir "$work/rtw89" \
    --cc "$(command -v "$CC")" --cflags="$flags"
if [ "${SAE_ANALYZE:-0}" = 1 ]; then
    "${PYTHON:-python3}" "$src/ember/tools/net80211-sae-analysis.py" \
        "$work/native/compile_commands.json" "$work/bsd/compile_commands.json" \
        "$work/rtw89/compile_commands.json"
fi
