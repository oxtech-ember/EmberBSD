#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/net80211-amsdu.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
python3 "$tools/net80211-amsdu-contract.py" \
    "${AMSDU_INPUT_SOURCE:-$src/sys/net80211/ieee80211_input.c}" "$work/native.inc"
mkdir -p "$work/sys" "$work/net80211"
printf '#include <string.h>\n#include <stdint.h>\n' > "$work/sys/systm.h"
ln -s "$src/sys/net80211/ieee80211_amsdu.h" "$work/net80211/ieee80211_amsdu.h"
extra=
if [ -f "$src/sys/net80211/ieee80211_amsdu.c" ]; then
    extra="$src/sys/net80211/ieee80211_amsdu.c"
fi
${CC:-gcc-16} -std=c11 -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE \
    -Wall -Wextra -Werror ${AMSDU_TEST_CFLAGS:-} -I"$work" \
    -I"$src/sys/net80211" "$tools/net80211-amsdu-contract.c" \
    $extra -o "$work/test"
if [ -n "${TEST_RUNNER:-}" ]; then
    "$TEST_RUNNER" "$work/test"
else
    "$work/test"
fi
