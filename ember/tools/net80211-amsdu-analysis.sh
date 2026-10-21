#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Analyze the same parser and extracted native consumer as the runtime contracts.
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/net80211-amsdu-analysis.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
mkdir -p "$work/sys" "$work/net80211"
printf '#include <string.h>\n#include <stdint.h>\n' > "$work/sys/systm.h"
ln -s "$src/sys/net80211/ieee80211_amsdu.h" "$work/net80211/ieee80211_amsdu.h"
python3 "$tools/net80211-amsdu-contract.py" \
    "$src/sys/net80211/ieee80211_input.c" "$work/native.inc"
${CPPCHECK:-cppcheck} --enable=warning,style,performance,portability \
    --error-exitcode=1 --check-level=exhaustive --inline-suppr \
    -D__packed= -I"$work" -I"$src/sys/net80211" \
    "$src/sys/net80211/ieee80211_amsdu.c" \
    "$tools/net80211-amsdu-parser-contract.c" \
    "$tools/net80211-amsdu-contract.c"
${CLANG:-clang} --analyze -Xanalyzer -analyzer-output=text \
    -Xanalyzer -analyzer-werror -std=c11 \
    -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -I"$work" -I"$src/sys/net80211" \
    "$src/sys/net80211/ieee80211_amsdu.c" \
    "$tools/net80211-amsdu-parser-contract.c" \
    "$tools/net80211-amsdu-contract.c"
CC=${CC:-gcc-16} AMSDU_TEST_CFLAGS='-g -gdwarf-4 -fanalyzer' \
    sh "$tools/net80211-amsdu-parser-contract.sh"
CC=${CC:-gcc-16} AMSDU_TEST_CFLAGS='-g -gdwarf-4 -fanalyzer' \
    sh "$tools/net80211-amsdu-contract.sh"
