#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD firmware width-order regression, AI-assisted (OpenAI Codex).
set -eu
[ "$#" -le 1 ] || exit 2
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
driver=${1:-"$src/ember/development/iwx-be200/src/if_iwx.c"}
work=$(mktemp -d "${TMPDIR:-/tmp}/iwx-width-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
python3 "$src/ember/tools/iwx-width-contract.py" --source "$driver" \
    --output "$work/contract.c"
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} \
    -I"$(dirname -- "$driver")" "$work/contract.c" -o "$work/test"
if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/test"; else "$work/test"; fi
