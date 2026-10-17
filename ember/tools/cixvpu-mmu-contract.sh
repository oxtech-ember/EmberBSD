#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD contributors, AI-assisted.
set -eu
[ "$#" -le 1 ] || exit 2
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)}
work=$(mktemp -d "${TMPDIR:-/tmp}/cixvpu-mmu.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
# CFLAGS is a caller-supplied compiler argument list.
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} \
    -I"$src/sys/dev/ic" "$src/sys/dev/ic/cixvpu_mmu.c" \
    "$src/ember/tools/cixvpu-mmu-contract.c" -o "$work/contract"
if [ -n "${CIXVPU_MMU_OUTPUT:-}" ]; then cp "$work/contract" "$CIXVPU_MMU_OUTPUT"; fi
if [ "${CIXVPU_COMPILE_ONLY:-0}" = 1 ]; then exit 0; fi
if [ -n "${TEST_RUNNER:-}" ]; then
    "$TEST_RUNNER" "$work/contract"
else
    "$work/contract"
fi
