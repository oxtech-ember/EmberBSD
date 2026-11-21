#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Compile the actual per-file VM handlers with kernel boundary models.
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/pvr-file.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
sed '/^#include /d' "$src/sys/external/bsd/drm2/powervr/pvr_drm.h" > "$work/pvr-uapi-body.h"
sed '/^#include /d' "$src/sys/external/bsd/drm2/powervr/pvr_file.c" > "$work/pvr-file-body.h"
# Retain the actual descriptor initializer and lifecycle callback fields.
sed -n '/^static const struct drm_ioctl_desc pvr_ioctls\[\] = {/,/^};/p' \
    "$src/sys/external/bsd/drm2/powervr/pvr_drv.c" > "$work/pvr-file-wire.h"
printf 'static struct test_driver test_driver = {\n' >> "$work/pvr-file-wire.h"
sed -n '/^[[:space:]]*\.open = /p; /^[[:space:]]*\.postclose = /p' \
    "$src/sys/external/bsd/drm2/powervr/pvr_drv.c" >> "$work/pvr-file-wire.h"
printf '};\n' >> "$work/pvr-file-wire.h"
# The actual red-black tree library with its invariant checks; header plumbing shimmed.
sed '/^#include <sys\/endian.h>/d' "$src/sys/sys/rbtree.h" > "$work/rbtree.h"
# Some host libc headers do not give uintptr_t here, or __RCSID.
cat > "$work/host-shim.h" <<'SHIM'
#include <stdint.h>
#if __has_include(<sys/cdefs.h>)
#include <sys/cdefs.h>
#endif
#ifndef __RCSID
#define __RCSID(x)
#endif
/*
 * Keep the RBDEBUG layout separate from the host libc ABI. libpthread
 * uses the host rb_tree functions with its own, non-debug objects.
 */
#define rb_tree_init pvr_test_rb_tree_init
#define rb_tree_insert_node pvr_test_rb_tree_insert_node
#define rb_tree_find_node pvr_test_rb_tree_find_node
#define rb_tree_find_node_geq pvr_test_rb_tree_find_node_geq
#define rb_tree_find_node_leq pvr_test_rb_tree_find_node_leq
#define rb_tree_remove_node pvr_test_rb_tree_remove_node
#define rb_tree_iterate pvr_test_rb_tree_iterate
#define rb_tree_check pvr_test_rb_tree_check
#define rb_tree_depths pvr_test_rb_tree_depths
SHIM
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
    ${CFLAGS:-} -DRBTEST -DRBDEBUG -D'__unused=__attribute__((unused))' \
    -D'__predict_false(x)=__builtin_expect((x) != 0, 0)' -include "$work/host-shim.h" -I"$work" \
    -c "$src/common/lib/libc/gen/rb.c" -o "$work/rb.o"
for shift in 12 14; do
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
        ${CFLAGS:-} -pthread -DRBDEBUG -DPAGE_SHIFT="$shift" -include "$work/host-shim.h" \
        -I"$work" -I"$src/sys/arch" -I"$src/sys/external/bsd/drm2/powervr" \
        "$src/ember/tools/pvr-file-contract.c" "$work/rb.o" -o "$work/contract"
    "$work/contract"
done
