#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Analyze the same parser and extracted native consumer as the runtime contracts.
set -eu
: "${CC:?select CC using the SDK run-native-check command}"
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/net80211-amsdu-analysis.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
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
"${CSA:-clang}" --analyze -Xanalyzer -analyzer-output=text \
    -Xanalyzer -analyzer-werror -std=c11 \
    -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -I"$work" -I"$src/sys/net80211" \
    "$src/sys/net80211/ieee80211_amsdu.c" \
    "$tools/net80211-amsdu-parser-contract.c" \
    "$tools/net80211-amsdu-contract.c"
CC="$CC" AMSDU_TEST_CFLAGS='-O0 -g -gdwarf-4 -fanalyzer' \
    sh "$tools/net80211-amsdu-parser-contract.sh"
CC="$CC" AMSDU_TEST_CFLAGS='-O0 -g -gdwarf-4 -fanalyzer --param=analyzer-max-constraints=100' \
    sh "$tools/net80211-amsdu-contract.sh"
CC="$CC" python3 "$tools/net80211-amsdu-analyzer-negative.py"
