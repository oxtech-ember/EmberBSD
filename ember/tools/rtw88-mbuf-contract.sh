#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Execute the actual native RX helper with allocation API doubles.
set -eu
[ "$#" -eq 0 ] || { echo "usage: $0" >&2; exit 2; }
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/rtw88-mbuf-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
ruby -rdigest -e 'abort "native m_devget reference changed" unless
    Digest::SHA256.file(ARGV[0]).hexdigest ==
    "cf81b8586d718b91dc25c7636c0cd512370510d311c1af546f57f6ecdf0cff9b"' \
    "$tools/rtw88-mbuf-devget-ref.inc"
mkdir -p "$work/include/sys" "$work/include/net" "$work/include/dev"
ln -s "$src/sys/dev/ic" "$work/include/dev/ic"
for header in sys/param.h sys/systm.h sys/mbuf.h net/if.h; do
    printf '#include "rtw88-mbuf-test.h"\n' > "$work/include/$header"
done
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${RTW88_TEST_CFLAGS:-} \
    -I"$work/include" -I"$tools" \
    "$tools/rtw88-mbuf-contract.c" "$src/sys/dev/ic/rtw88_net_mbuf.c" \
    -o "$work/test"
if [ -n "${TEST_RUNNER:-}" ]; then
    "$TEST_RUNNER" "$work/test"
else
    "$work/test"
fi
