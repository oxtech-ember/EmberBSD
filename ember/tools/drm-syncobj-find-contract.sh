#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD native syncobj publication contract.
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/drm-sync-find.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
file="$src/sys/external/bsd/drm2/dist/drm/drm_syncobj.c"
awk '/^static void drm_syncobj_fence_add_wait/ { take=1 }
    take && /^static void drm_syncobj_remove_wait/ { exit }
    take { print }' "$file" > "$work/add-body.h"
awk '/^int drm_syncobj_find_fence/ { take=1 }
    take && /^EXPORT_SYMBOL/ { exit }
    take { print }' "$file" > "$work/find-body.h"
# The first occurrence is a forward declaration. Select the definition.
awk '/^static void syncobj_wait_syncobj_func/ { body=$0; next }
    body && !take { body=body "\n" $0; if ($0 ~ /;$/) body="";
        else if ($0 == "{") { print body; take=1 }; next }
    take { print } take && /^}$/ { exit }' "$file" > "$work/callback-body.h"
awk '/^drm_syncobj_transfer_ioctl\(/ { take=1; print "int" }
    take { print } take && /^}$/ { exit }' "$file" > "$work/transfer-body.h"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
    ${CFLAGS:-} -I"$work" "$src/ember/tools/drm-syncobj-find-contract.c" -o "$work/check"
for scenario in ${DRM_FIND_CASES:-1 2 3 4 5 6 7 8 9 10 11 12}; do
    "$work/check" "$scenario"
done
