#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
: "${CC:?select CC using SDK run-native-check}"
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/rtw88-pcie.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work/dev"
ln -s "$src/sys/dev/ic" "$work/dev/ic"
ln -s "$src/sys/dev/pci" "$work/dev/pci"
"$CC" -std=c11 -Wall -Wextra -Werror ${RTW88_TEST_CFLAGS:-} \
    -I"$work" -I"$tools" "$tools/rtw88-pcie-phy-contract.c" -o "$work/test"
"$work/test"
