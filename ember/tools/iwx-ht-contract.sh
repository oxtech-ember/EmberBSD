#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD iwx(4) HT/VHT negotiation contract, AI-assisted.
set -eu
[ "$#" -eq 0 ] || exit 2
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/iwx-ht-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} \
    -I"$src/ember/development/iwx-be200/src" \
    "$src/ember/tools/iwx-ht-contract.c" -o "$work/test"
if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/test"; else "$work/test"; fi
