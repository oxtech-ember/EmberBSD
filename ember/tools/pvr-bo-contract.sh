#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD production PowerVR BO and native GEM lifetime contract.
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/pvr-bo.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
sed '/^#include /d' "$src/sys/external/bsd/drm2/powervr/pvr_drm.h" \
    > "$work/pvr-uapi-body.h"
sed '/^#include /d' "$src/sys/external/bsd/drm2/powervr/pvr_bo.c" \
    > "$work/pvr-bo-body.h"
sed '/^#include /d' "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_mmu.h" \
    > "$work/pvr-mmu-body.h"
printf 'int\n' > "$work/pvr-gem-core-body.h"
sed -n '/^drm_gem_close_ioctl(/,/^}/p' \
    "$src/sys/external/bsd/drm2/dist/drm/drm_gem.c" >> "$work/pvr-gem-core-body.h"
for name in drm_gem_pager_reference drm_gem_pager_detach; do
    printf 'void\n' >> "$work/pvr-gem-core-body.h"
    sed -n "/^$name(/,/^}/p" "$src/sys/external/bsd/drm2/drm/drm_gem_vm.c" \
        >> "$work/pvr-gem-core-body.h"
done
for shift in 12 14; do
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
        ${CFLAGS:-} -DPAGE_SHIFT="$shift" -I"$work" \
        "$src/ember/tools/pvr-bo-contract.c" -o "$work/contract"
    "$work/contract"
done
