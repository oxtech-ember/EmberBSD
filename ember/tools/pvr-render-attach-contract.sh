#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD production GPU render registration lock contract.
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/pvr-render-attach.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
driver="$src/sys/arch/arm/sunxi/sun60i_a733_gpu.c"
for name in sun60i_gpu_query_input sun60i_gpu_render_attach \
    sun60i_gpu_sysctl_render_attach; do
    printf 'static int\n' >> "$work/pvr-attach-body.h"
    sed -n "/^$name(/,/^}/p" "$driver" >> "$work/pvr-attach-body.h"
done
sed '/^#include /d' "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwmem.h" \
    > "$work/fwmem-body.h"
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} -I"$work" \
    -I"$src/sys/arch/arm/sunxi" -I"$src/sys/external/bsd/drm2/powervr" \
    "$src/ember/tools/pvr-render-attach-contract.c" \
    "$src/sys/arch/arm/sunxi/sun60i_a733_gpu_fwinfo.c" -o "$work/contract"
"$work/contract"
