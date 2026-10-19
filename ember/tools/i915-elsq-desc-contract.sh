#!/bin/sh
# Origin: EmberBSD Jasper Lake execlist submission regression, 2026-10-19.
# SPDX-License-Identifier: BSD-2-Clause
# Extract the actual native i915 write_desc(). Model the bus space writes.
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/i915-elsq.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
awk '
	/^static inline void write_desc\(/ { copying = 1 }
	copying { print }
	copying && /^}/ { exit }
' "$src/sys/external/bsd/drm2/dist/drm/i915/gt/intel_lrc.c" \
    > "$work/write-desc-body.h"
grep -q '^static inline void write_desc(' "$work/write-desc-body.h"
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
    ${CFLAGS:-} -I"$work" "$src/ember/tools/i915-elsq-desc-contract.c" \
    -o "$work/contract"
"$work/contract"
