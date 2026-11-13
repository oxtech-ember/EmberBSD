#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
#
# check-statics.sh [drm2-dir]  -- the DEFINE_MUTEX/SPINLOCK/XARRAY_ALLOC/
# STATIC_SRCU objects of the built files of dist618 must be exactly the ones in
# the table of drm618/drm618_statics.c (see include/drm618_statics.h).  Files in
# tools/not-built.txt are not looked at.  Exit 0 and "ok" if they agree.
D=$(cd "${1:-$(dirname "$0")/../..}" && pwd)
want=$(sed -n '/drm618-static-objects: begin/,/drm618-static-objects: end/p' "$D/drm618/drm618_statics.c" |
    sed -n 's/^[[:space:]]*X(\([A-Za-z0-9_]*\)).*/\1/p' | sort)
have=$(cd "$D/dist618" &&
    for f in $(grep -E '^(drm|gpu)/.*\.c	' MANIFEST.tsv | cut -f1); do
	grep -qx "$f" "$D/drm618/tools/not-built.txt" && continue
	grep -hE '^[[:space:]]*(static[[:space:]]+)?DEFINE_(MUTEX|SPINLOCK|XARRAY_ALLOC|STATIC_SRCU)\(' "$f"
    done | sed 's/.*DEFINE_[A-Z_]*(\([A-Za-z0-9_]*\)).*/\1/' | sort)
if [ "$want" = "$have" ]; then echo ok; exit 0; fi
echo "table:"; echo "$want"; echo "dist618:"; echo "$have"
echo "check-statics: MISMATCH"; exit 1
