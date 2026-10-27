#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Analyze the actual parser and extracted native scan/cleanup/copyout consumers.
set -eu
: "${CC:?select CC using the SDK run-native-check command}"
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/net80211-phy-analysis.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
CC="$CC" PHY_IES_TEST_CFLAGS='-O0 -g -gdwarf-4 -fanalyzer' \
    sh "$src/ember/tools/net80211-phy-ies-parser-contract.sh"
PHY_IES_TEST_CFLAGS='-O0 -g -gdwarf-4 -fanalyzer' PHY_IES_TEST_KEEP=1 TMPDIR="$work" \
    sh "$src/ember/tools/net80211-phy-ies-contract.sh"
# The extraction directory is retained by the contract inside this exact work root.
fixture=$(find "$work" -name native-node.inc -type f)
fixture_dir=$(dirname "$fixture")
"$CC" -std=c11 -dM -E - < /dev/null > "$work/predefines.h"
# Cppcheck does not search the compiler's system include directories.
# Supply the actual host queue header, not a replacement queue implementation.
printf '#include <sys/queue.h>\n' | "$CC" -E -H -x c - \
    > /dev/null 2> "$work/queue-headers.txt"
queue_header=$(sed -n 's/^\. \(.*sys\/queue.h\)$/\1/p' "$work/queue-headers.txt")
[ -n "$queue_header" ]
cppcheck --enable=warning,performance,portability --error-exitcode=1 \
    --check-level=exhaustive --include="$work/predefines.h" \
    --include="$queue_header" -UIEEE80211_DEBUG \
    -I"$fixture_dir" -I"$src/sys/net80211" \
    "$src/sys/net80211/ieee80211_phy_ies.c" \
    "$src/ember/tools/net80211-phy-ies-parser-contract.c" \
    "$src/ember/tools/net80211-phy-ies-contract.c"
"${CSA:?select clang using SDK --required-role clang}" --analyze -std=c11 -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE \
    -Xanalyzer -analyzer-output=text -Xanalyzer -analyzer-werror \
    -I"$fixture_dir" -I"$src/sys/net80211" \
    "$src/sys/net80211/ieee80211_phy_ies.c" \
    "$src/ember/tools/net80211-phy-ies-parser-contract.c" \
    "$src/ember/tools/net80211-phy-ies-contract.c"
