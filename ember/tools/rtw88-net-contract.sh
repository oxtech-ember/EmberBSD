#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Original native RTL8822CE net policy checks, AI-assisted.
set -eu
[ "$#" -eq 0 ] || { echo "usage: $0" >&2; exit 2; }
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/rtw88-net-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
${CC:-cc} -std=c11 -Wall -Wextra -Werror -pthread ${RTW88_TEST_CFLAGS:-} \
    -I"$src/sys/dev/ic" "$tools/rtw88-net-contract.c" \
    "$src/sys/dev/ic/rtw88_net_policy.c" -o "$work/test"
if [ -n "${TEST_RUNNER:-}" ]; then
    "$TEST_RUNNER" "$work/test"
else
    "$work/test"
fi
# Execute the real negotiation and the native wrapper's rate-mask consumer.
# Retain the source notices in the temporary extraction. Do not copy a model.
rate_source=${RTW88_RATE_SOURCE:-"$src/sys/net80211/ieee80211_proto.c"}
mkdir "$work/net80211"
cp "$src/sys/net80211/_ieee80211.h" "$work/net80211/"
awk 'BEGIN { copying=1 } copying { print; if ($0 == " */") copying=0 }' \
    "$rate_source" > "$work/rate-native.inc"
awk '/^ieee80211_fix_rate\(/ { print "int"; copying=1 }
    copying { print } copying && /^}/ { exit }' \
    "$rate_source" >> "$work/rate-native.inc"
awk '/^rtw88_net_basic_rates\(/ { print "static unsigned"; copying=1 }
    copying { print } copying && /^}/ { exit }' \
    "$src/sys/dev/ic/rtw88_net.c" > "$work/rate-consumer.inc"
awk '/^#define[[:space:]]+IEEE80211_RATE_(BASIC|VAL)[[:space:]]/ { print }' \
    "$src/sys/net80211/ieee80211.h" > "$work/rate-constants.inc"
awk '/^#define[[:space:]]+IEEE80211_FIXED_RATE_NONE[[:space:]]/ { print }' \
    "$src/sys/net80211/ieee80211_var.h" >> "$work/rate-constants.inc"
awk '/^#define[[:space:]]+IEEE80211_R_DO(SORT|FRATE|NEGO|DEL)[[:space:]]/ { print }' \
    "$src/sys/net80211/ieee80211_proto.h" >> "$work/rate-constants.inc"
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${RTW88_TEST_CFLAGS:-} \
    -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE '-D__arraycount(a)=(sizeof(a)/sizeof((a)[0]))' \
    -I"$work" "$tools/rtw88-rate-contract.c" -o "$work/rate-test"
if [ -n "${TEST_RUNNER:-}" ]; then
    "$TEST_RUNNER" "$work/rate-test"
else
    "$work/rate-test"
fi
