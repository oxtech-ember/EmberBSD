#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD actual A133 GPU FDT preflight checks, AI-assisted.
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/a133-gpu-preflight.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
source="$src/sys/arch/arm/sunxi/sun50i_a133_gpu.c"
printf 'static int a133_gpu_reserved_supported(void);\n' > "$work/preflight.inc"
for spec in 'void a133_gpu_diag_reset' 'void a133_gpu_preflight_release' 'bool a133_gpu_identity_dma' \
    'bool a133_gpu_opp_table' 'bool a133_gpu_irq' 'int a133_gpu_preflight' \
    'int a133_gpu_activate' 'int a133_gpu_live' 'int a133_gpu_prepare' \
    'int a133_gpu_reserved_supported' 'int a133_gpu_start'; do
    type=${spec%% *}; name=${spec#* }
    printf 'static %s\n' "$type" >> "$work/preflight.inc"
    sed -n "/^$name(/,/^}/p" "$source" >> "$work/preflight.inc"
done
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} -I"$work" -I"$src/sys/arch/arm/sunxi" \
    "$src/ember/tools/a133-gpu-preflight-contract.c" \
    "$src/sys/arch/arm/sunxi/sun50i_a133_gpu_owner.c" -o "$work/test"
if [ -n "${TEST_RUNNER:-}" ]; then
    $TEST_RUNNER "$work/test"
else
    "$work/test"
fi
