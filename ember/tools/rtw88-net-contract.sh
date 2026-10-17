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
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${RTW88_TEST_CFLAGS:-} \
    -I"$src/sys/dev/ic" "$tools/rtw88-net-contract.c" \
    "$src/sys/dev/ic/rtw88_net_policy.c" -o "$work/test"
"$work/test"
