#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
: "${CC:?select CC using the SDK run-native-check command}"
[ "$#" -eq 0 ] || exit 2
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/net80211-crypto-bounds.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
awk 'BEGIN { copying=1 } copying { print; if ($0 == " */") copying=0 }' \
    "$src/sys/net80211/ieee80211_crypto.c" > "$work/native-crypto-bounds.inc"
awk '/^ieee80211_crypto_decap\(/ { print "struct ieee80211_key *"; copying=1 }
    copying { print } copying && /^}/ { exit }' \
    "$src/sys/net80211/ieee80211_crypto.c" >> "$work/native-crypto-bounds.inc"
"$CC" -std=c11 -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE \
    -Wall -Werror ${CCMP_TEST_CFLAGS:-} \
    -I"$work" "$tools/net80211-crypto-bounds-contract.c" -o "$work/test"
if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/test"; else "$work/test"; fi
if [ "${CCMP_ANALYZE:-0}" = 1 ]; then
    "$CC" -std=c11 -dM -E - < /dev/null > "$work/predefines.h"
    cppcheck --enable=warning,performance,portability --error-exitcode=1 \
        --check-level=exhaustive --include="$work/predefines.h" \
        -I"$work" "$tools/net80211-crypto-bounds-contract.c"
    "${CSA:-clang}" --analyze -std=c11 -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE \
        -Wall -Werror \
        -Xanalyzer -analyzer-output=text -Xanalyzer -analyzer-werror \
        -I"$work" "$tools/net80211-crypto-bounds-contract.c" -o "$work/analysis.plist"
fi
