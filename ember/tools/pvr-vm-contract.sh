#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Compile the actual CPU-owned VM and MMU encoders with DMA boundary models.
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/pvr-vm.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
sed '/^#include /d' "$src/sys/external/bsd/drm2/powervr/pvr_vm.c" \
    > "$work/pvr-vm-body.h"
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
    ${CFLAGS:-} -I"$work" -I"$src/sys/arch/arm/sunxi" \
    "$src/ember/tools/pvr-vm-contract.c" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_mmu.c" -o "$work/contract"
"$work/contract"
