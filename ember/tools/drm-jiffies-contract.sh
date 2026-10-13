#!/bin/sh
# Origin: EmberBSD native DRM timeout regression, 2026-10-11.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/drm-jiffies.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
awk '/^nsecs_to_jiffies64\(/ { print "static uint64_t"; take=1 }
    take { print } take && /^}/ { exit }' \
    "$src/sys/external/bsd/drm2/include/linux/jiffies.h" > "$work/jiffies-body.h"
awk '/^signed long drm_timeout_abs_to_jiffies/ { take=1 }
    take { print } take && /^}/ { exit }' \
    "$src/sys/external/bsd/drm2/dist/drm/drm_syncobj.c" > "$work/timeout-body.h"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} -I"$work" \
    "$src/ember/tools/drm-jiffies-contract.c" -o "$work/check"
"$work/check"
