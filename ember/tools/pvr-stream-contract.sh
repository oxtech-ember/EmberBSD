#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Compile the actual native decoder against its pinned firmware ABI headers.
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/pvr-stream.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} \
    -I"$src/sys/arch" -I"$src/sys/external/bsd/drm2/powervr" \
    "$src/ember/tools/pvr-stream-contract.c" \
    "$src/sys/external/bsd/drm2/powervr/pvr_stream.c" -o "$work/contract"
"$work/contract"
