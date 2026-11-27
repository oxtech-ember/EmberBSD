#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD actual bounded HE wire codec contract.
set -eu
: "${CC:?select CC using the SDK run-native-check command}"
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
if ! awk '
	/^INCS=/ { active=1 }
	active && /ieee80211_he_ies[.]h/ { found=1 }
	END { exit !found }
' "$src/sys/net80211/Makefile"; then
	echo "HE contract header is missing from installed net80211 headers" >&2
	exit 1
fi
work=$(mktemp -d "${TEST_OBJDIR:-${TMPDIR:-/tmp}}/net80211-he-ies.XXXXXXXX")
if [ "${HE_IES_TEST_KEEP:-0}" = 1 ]; then
    printf 'Test sources: %s\n' "$work"
else
    trap 'rm -rf "$work"' EXIT HUP INT TERM
fi
mkdir -p "$work/sys" "$work/net80211"
ln -s "$src/sys/net80211/ieee80211_he_ies.h" "$work/net80211/ieee80211_he_ies.h"
printf '#include <stdbool.h>\n#include <stdint.h>\n#include <string.h>\n' > "$work/sys/systm.h"
"$CC" -std=c11 -Wall -Wextra -Werror ${HE_IES_TEST_CFLAGS:-} \
    -include stdbool.h -include stdint.h -I"$work" \
    "$src/ember/tools/net80211-he-ies-contract.c" \
    "$src/sys/net80211/ieee80211_he_ies.c" -o "$work/check"
if [ -n "${TEST_RUNNER:-}" ]; then
    "$TEST_RUNNER" "$work/check"
else
    "$work/check"
fi
