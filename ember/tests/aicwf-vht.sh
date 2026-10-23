#!/bin/sh
# Check the production VHT builder and bounded association decoder.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TEST_OBJDIR:-${TMPDIR:-/tmp}}/aicwf-vht.XXXXXXXX")
if [ "${AICWF_VHT_KEEP:-0}" = 1 ]; then
    printf 'Test outputs: %s\n' "$work"
else
    trap 'rm -rf "$work"' EXIT HUP INT TERM
fi
mkdir -p "$work/sys" "$work/net80211"
ln -s "$src/sys/net80211/ieee80211_vht_ies.h" "$work/net80211/ieee80211_vht_ies.h"
printf '#include <stdbool.h>\n#include <stdint.h>\n#include <string.h>\n' > "$work/sys/systm.h"
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${AICWF_VHT_CFLAGS:-} \
    -include stdbool.h -include stdint.h -I"$work" -I"$src/sys/dev/sdmmc" "$src/ember/tools/aicwf-vht-test.c" \
    "$src/sys/net80211/ieee80211_vht_ies.c" -o "$work/check"
if [ -n "${TEST_RUNNER:-}" ]; then
    "$TEST_RUNNER" "$work/check"
else
    "$work/check"
fi
