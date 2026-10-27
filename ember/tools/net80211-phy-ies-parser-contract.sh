#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
: "${CC:?select CC using the SDK run-native-check command}"
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/net80211-phy-ies-parser.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work/sys" "$work/net80211"
printf '#include <string.h>\n#include <stdint.h>\n' > "$work/sys/systm.h"
ln -s "$src/sys/net80211/ieee80211_phy_ies.h" "$work/net80211/ieee80211_phy_ies.h"
"$CC" -std=c11 -Wall -Wextra -Werror ${PHY_IES_TEST_CFLAGS:-} \
    -I"$work" -I"$src/sys/net80211" \
    "$tools/net80211-phy-ies-parser-contract.c" \
    "$src/sys/net80211/ieee80211_phy_ies.c" -o "$work/test"
if [ -n "${TEST_RUNNER:-}" ]; then
    "$TEST_RUNNER" "$work/test"
else
    "$work/test"
fi
