#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# AI-assisted EmberBSD native sync_file readiness contract.
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/drm-sync-file.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
driver=${SYNC_FILE_SOURCE:-$src/sys/external/bsd/drm2/linux/linux_sync_file.c}
awk '/^sync_file_fence_cb\(/ { taking=1; print "static void" }
    /^static const struct filterops/ { exit }
    taking { print }' "$driver" > "$work/sync-file-body.h"
grep -q '^sync_file_poll(' "$work/sync-file-body.h"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
    ${CFLAGS:-} -I"$work" "$src/ember/tools/drm-sync-file-contract.c" -o "$work/check"
"$work/check"
