#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
#
# check-statics.sh [drm2-dir]  -- the DEFINE_MUTEX/SPINLOCK/XARRAY_ALLOC/
# STATIC_SRCU objects of the built files must be exactly the ones in the
# tables of the modules:
#   - drmkms618: the table of drm618/drm618_statics.c, against the built
#     files of dist618 (not drm/ttm, not tools/not-built.txt) and the own
#     sources drm618/drm618_*.c;
#   - drmkms618_ttm: the drm618_static_init_* declarations of
#     drm618/drm618_ttm_module.c, against dist618/drm/ttm.
# See include/drm618_statics.h.  Exit 0 and "ok" if they agree.
D=$(cd "${1:-$(dirname "$0")/../..}" && pwd)
objects() {
	grep -hE '^[[:space:]]*(static[[:space:]]+)?DEFINE_(MUTEX|SPINLOCK|XARRAY_ALLOC|STATIC_SRCU)\(' "$@" |
	    sed 's/.*DEFINE_[A-Z_]*(\([A-Za-z0-9_]*\)).*/\1/'
}
want=$(sed -n '/drm618-static-objects: begin/,/drm618-static-objects: end/p' "$D/drm618/drm618_statics.c" |
    sed -n 's/^[[:space:]]*X(\([A-Za-z0-9_]*\)).*/\1/p' | sort)
have=$( (cd "$D/dist618" &&
    for f in $(grep -E '^(drm|gpu)/.*\.c	' MANIFEST.tsv | cut -f1); do
	grep -qx "$f" "$D/drm618/tools/not-built.txt" && continue
	case $f in drm/ttm/*) continue;; esac
	objects "$f"
    done
    cd "$D/drm618" &&
    for f in drm618_*.c; do
	[ "$f" = drm618_statics.c ] && continue
	objects "$f"
    done) | sort)
want_ttm=$(sed -n 's/^void[[:space:]]*drm618_static_init_\([A-Za-z0-9_]*\)(void);.*/\1/p' \
    "$D/drm618/drm618_ttm_module.c" | sort)
have_ttm=$( (cd "$D/dist618" && for f in $(grep -E '^drm/ttm/.*\.c	' MANIFEST.tsv | cut -f1); do
	objects "$f"
    done) | sort)
rc=0
if [ "$want" != "$have" ]; then
	echo "drmkms618 table:"; echo "$want"; echo "sources:"; echo "$have"; rc=1
fi
if [ "$want_ttm" != "$have_ttm" ]; then
	echo "drmkms618_ttm table:"; echo "$want_ttm"; echo "dist618/drm/ttm:"; echo "$have_ttm"; rc=1
fi
if [ $rc = 0 ]; then echo ok; exit 0; fi
echo "check-statics: MISMATCH"; exit 1
