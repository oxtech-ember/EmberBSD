#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors. AI-assisted real-source tests.
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ "$#" -gt 1 ]; then
    echo "usage: $0 [source-root]" >&2
    exit 2
fi
src=${1:-$(CDPATH= cd -- "$tools/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/cixvpu-ring.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
# CC, CFLAGS, and TEST_RUNNER intentionally accept shell-separated arguments.
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} \
    -I"$src/sys/dev/ic" "$src/sys/dev/ic/cixvpu_ring.c" \
    "$src/ember/tools/cixvpu-ring-contract.c" -o "$work/contract"
${TEST_RUNNER:-} "$work/contract"
