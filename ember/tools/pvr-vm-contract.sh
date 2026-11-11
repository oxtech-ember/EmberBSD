#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Compile the actual CPU-owned VM and MMU encoders with DMA boundary models.
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/pvr-vm.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
sed '/^#include /d' "$src/sys/external/bsd/drm2/powervr/pvr_vm.c" \
    > "$work/pvr-vm-body.h"
has_pages=0
if grep -q '^pvr_vm_map_pages(' "$src/sys/external/bsd/drm2/powervr/pvr_vm.c"; then
    has_pages=1
fi
# The actual red-black tree library with its own invariant checks (RBDEBUG
# changes the node layout, so both objects use it); only header plumbing is shimmed.
sed '/^#include <sys\/endian.h>/d' "$src/sys/sys/rbtree.h" > "$work/rbtree.h"
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
    ${CFLAGS:-} -DRBTEST -DRBDEBUG -D'__unused=__attribute__((unused))' \
    -D'__predict_false(x)=__builtin_expect((x) != 0, 0)' -I"$work" \
    -c "$src/common/lib/libc/gen/rb.c" -o "$work/rb.o"
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
    ${CFLAGS:-} -DRBDEBUG -DPVR_VM_TEST_MAP_PAGES="$has_pages" -I"$work" -I"$src/sys/arch/arm/sunxi" \
    "$src/ember/tools/pvr-vm-contract.c" "$work/rb.o" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_mmu.c" \
    "$src/sys/dev/powervr/pvr_mmu.c" -o "$work/contract"
"$work/contract"
