#!/bin/sh
# Check BSD cipher diagnostics against the actual net80211 cipher numbers.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
driver=${BSD_WPA_CIPHER_SOURCE:-$src/external/bsd/wpa/dist/src/drivers/driver_bsd.c}
work=$(mktemp -d "${TMPDIR:-/tmp}/bsd-wpa-cipher.XXXXXXXX")
if [ "${BSD_WPA_CIPHER_KEEP:-0}" = 1 ]; then
    printf 'Test sources: %s\n' "$work"
else
    trap 'rm -rf "$work"' EXIT HUP INT TERM
fi
awk '/^bsd_configure_wpa\(/ {copy=1}
    copy {print} copy && /^}/ {exit}' "$driver" > "$work/configure.h"
test -s "$work/configure.h"
awk '/^#define[[:space:]]+IEEE80211_CIPHER_/ {print}' \
    "$src/sys/net80211/ieee80211_crypto.h" > "$work/ciphers.h"
awk '/^#define[[:space:]]+IEEE80211_IOC_(MCASTCIPHER|MCASTKEYLEN|UCASTCIPHERS|KEYMGTALGS|RSNCAPS|WPA)[[:space:]]/ {print}' \
    "$src/sys/net80211/ieee80211_ioctl.h" > "$work/ioctls.h"
${CC:-cc} -std=c99 -Wall -Wextra -Werror -Wno-unused-parameter ${CFLAGS:-} \
    -D_DEFAULT_SOURCE -I"$src/external/bsd/wpa/dist/src" -I"$work" \
    "$src/ember/tools/bsd-wpa-cipher.c" -o "$work/check"
"$work/check"
