#!/bin/sh
# Origin: EmberBSD native DRM wait regression, 2026-10-11.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/drm-sync-wait.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
awk '/^static void syncobj_wait_fence_func/ { take=1 }
    take && /^\/\*\*/ { exit }
    take { print }' "$src/sys/external/bsd/drm2/dist/drm/drm_syncobj.c" > "$work/wait-body.h"
# Select the native branch after including host system headers.
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
    ${CFLAGS:-} -I"$work" "$src/ember/tools/drm-syncobj-wait-contract.c" -o "$work/check"
for scenario in ${DRM_WAIT_CASES:-1 2 3 4}; do "$work/check" "$scenario"; done
