#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD actual bounded VHT wire codec contract.
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TEST_OBJDIR:-${TMPDIR:-/tmp}}/net80211-vht-ies.XXXXXXXX")
if [ "${VHT_IES_TEST_KEEP:-0}" = 1 ]; then
    printf 'Test sources: %s\n' "$work"
else
    trap 'rm -rf "$work"' EXIT HUP INT TERM
fi
mkdir -p "$work/sys" "$work/net80211"
ln -s "$src/sys/net80211/ieee80211_vht_ies.h" "$work/net80211/ieee80211_vht_ies.h"
printf '#include <stdbool.h>\n#include <stdint.h>\n#include <string.h>\n' > "$work/sys/systm.h"
${CC:-gcc-16} -std=c11 -Wall -Wextra -Werror ${VHT_IES_TEST_CFLAGS:-} \
    -include stdbool.h -include stdint.h -I"$work" \
    "$src/ember/tools/net80211-vht-ies-contract.c" \
    "$src/sys/net80211/ieee80211_vht_ies.c" -o "$work/check"
if [ -n "${TEST_RUNNER:-}" ]; then
    "$TEST_RUNNER" "$work/check"
else
    "$work/check"
fi
