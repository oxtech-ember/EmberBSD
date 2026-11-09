#!/bin/sh
# Origin: EmberBSD Realtek Bluetooth patch image contract, AI-assisted.
# SPDX-License-Identifier: BSD-2-Clause
# Compile usr.sbin/rtlbtfw/rtlbt_fw.c with its host checks and run them.
# Set RTLBT_FIRMWARE to a linux-firmware rtl8821c_fw.bin for an extra check.
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=${RTLBTFW_SOURCE_ROOT:-$(CDPATH= cd -- "$tools/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/rtlbtfw-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT
trap 'exit 1' HUP INT TERM
${HOST_CC:-cc} ${HOST_CFLAGS:--O1 -g -std=c11 -Wall -Wextra -Werror} \
    ${RTLBTFW_TEST_CFLAGS:-} -I"$src/usr.sbin/rtlbtfw" \
    "$src/usr.sbin/rtlbtfw/rtlbt_fw.c" "$tools/rtlbtfw-contract.c" \
    -o "$work/rtlbtfw-contract"
${TEST_RUNNER:-} "$work/rtlbtfw-contract"
